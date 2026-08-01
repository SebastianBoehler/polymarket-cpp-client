#include "websocket_callback_context.hpp"
#include "websocket_client_state.hpp"
#include "websocket_resilience.hpp"

namespace polymarket::detail
{
    std::shared_ptr<WebSocketClientState> WebSocketClientState::create()
    {
        return std::shared_ptr<WebSocketClientState>(new WebSocketClientState());
    }

    WebSocketClientState::WebSocketClientState()
        : message_queue_(std::make_unique<BoundedMessageQueue>(options_.message_queue_limit))
    {
        apply_options_locked();
    }

    WebSocketClientState::~WebSocketClientState()
    {
        ws_.setOnMessageCallback(nullptr);
        request_message_worker_stop();
        if (message_worker_.joinable())
        {
            if (message_worker_.get_id() == std::this_thread::get_id())
                message_worker_.detach();
            else
                message_worker_.join();
        }
        ws_.stop();
    }

    void WebSocketClientState::set_url(const std::string &url)
    {
        std::lock_guard<std::mutex> lock(lifecycle_mutex_);
        require_inactive_locked();
        ws_.setUrl(url);
    }

    bool WebSocketClientState::connect()
    {
        if (is_internal_callback(this) && transport_stop_pending_.load()) return false;
        if (transport_stop_pending_.load()) wait_for_transport_stop();
        if (state_.load() == WsState::DISCONNECTED)
        {
            bool active = false;
            {
                std::lock_guard<std::mutex> lock(transport_mutex_);
                active = transport_worker_active_;
            }
            if (active)
            {
                request_transport_stop();
                if (is_internal_callback(this)) return false;
                wait_for_transport_stop();
            }
        }

        std::exception_ptr failure;
        bool deferred_cleanup = false;
        {
            std::lock_guard<std::mutex> lock(lifecycle_mutex_);
            const auto current = state_.load();
            if (current == WsState::CONNECTED || current == WsState::CONNECTING ||
                current == WsState::RECONNECTING)
            {
                return true;
            }
            if (current == WsState::CLOSING) return false;

            try
            {
                start_transport_worker();
                install_transport_callback();
                state_.store(WsState::CONNECTING);
                resnapshot_pending_.store(false);
                should_stop_.store(false);
                start_message_worker();
                ws_.start();
            }
            catch (...)
            {
                state_.store(WsState::CLOSING);
                request_message_worker_stop();
                deferred_cleanup = request_transport_stop();
                failure = std::current_exception();
            }
        }

        if (!failure) return true;
        if (deferred_cleanup)
            wait_for_transport_stop();
        else
        {
            finish_disconnect();
            publish_disconnected();
        }
        std::rethrow_exception(failure);
    }

    void WebSocketClientState::install_transport_callback()
    {
        auto self = shared_from_this();
        ws_.setOnMessageCallback([self](const ix::WebSocketMessagePtr &message)
                                 { self->handle_transport_message(message); });
    }

    void WebSocketClientState::handle_transport_message(
        const ix::WebSocketMessagePtr &message)
    {
        InternalCallbackScope callback_scope(this);
        switch (message->type)
        {
        case ix::WebSocketMessageType::Open:
            handle_open();
            break;
        case ix::WebSocketMessageType::Close:
            handle_close();
            break;
        case ix::WebSocketMessageType::Error:
            handle_error(message);
            break;
        case ix::WebSocketMessageType::Message:
            messages_received_++;
            bytes_received_ += message->str.size();
            last_message_time_ns_.store(now_ns());
            enqueue_message(message->str);
            break;
        case ix::WebSocketMessageType::Ping:
        case ix::WebSocketMessageType::Pong:
        case ix::WebSocketMessageType::Fragment:
            break;
        }
    }

    void WebSocketClientState::handle_open()
    {
        mark_stream_gap();
        {
            std::lock_guard<std::mutex> lock(lifecycle_mutex_);
            if (transport_stop_pending_.load()) return;
            if (resnapshot_pending_.exchange(false) && !options_.reconnect_enabled)
                ws_.disableAutomaticReconnection();
            state_.store(WsState::CONNECTED);
        }
        state_cv_.notify_all();
        if (has_connected_.exchange(true)) reconnects_++;
        if (!restore_subscriptions()) return;

        auto callbacks = callbacks_snapshot();
        invoke_user_callback("connect", callbacks->connect);
    }

    void WebSocketClientState::handle_close()
    {
        if (!transport_stop_pending_.load()) mark_stream_gap();
        {
            std::lock_guard<std::mutex> lock(lifecycle_mutex_);
            if (!transport_stop_pending_.load())
            {
                const bool reconnect = options_.reconnect_enabled ||
                                       resnapshot_pending_.load();
                state_.store(reconnect ? WsState::RECONNECTING
                                       : WsState::DISCONNECTED);
                state_cv_.notify_all();
            }
        }

        if (transport_stop_pending_.load()) return;
        auto callbacks = callbacks_snapshot();
        invoke_user_callback("disconnect", callbacks->disconnect);
    }

    void WebSocketClientState::handle_error(const ix::WebSocketMessagePtr &message)
    {
        if (transport_stop_pending_.load()) return;
        mark_stream_gap();

        bool reconnect = false;
        {
            std::lock_guard<std::mutex> lock(lifecycle_mutex_);
            if (transport_stop_pending_.load()) return;
            const bool limit_reached = reconnect_limit_reached(
                message->errorInfo.retries, options_.max_reconnect_attempts);
            if (limit_reached) ws_.disableAutomaticReconnection();
            reconnect = (options_.reconnect_enabled || resnapshot_pending_.load()) &&
                        !limit_reached;
            state_.store(reconnect ? WsState::RECONNECTING : WsState::DISCONNECTED);
        }
        state_cv_.notify_all();

        invoke_error_callback(message->errorInfo.reason);
    }

    bool WebSocketClientState::wait_until_connected(std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock(state_wait_mutex_);
        state_cv_.wait_for(lock, timeout, [this]
                           {
            const auto current = state_.load();
            return current == WsState::CONNECTED || current == WsState::DISCONNECTED; });
        return is_connected();
    }

    void WebSocketClientState::disconnect()
    {
        bool deferred = false;
        {
            std::lock_guard<std::mutex> lock(lifecycle_mutex_);
            state_.store(WsState::CLOSING);
            resnapshot_pending_.store(false);
            if (!options_.reconnect_enabled) ws_.disableAutomaticReconnection();
            request_message_worker_stop();
            deferred = request_transport_stop();
        }

        if (!deferred)
        {
            finish_disconnect();
            publish_disconnected();
            return;
        }
        if (!is_internal_callback(this)) wait_for_transport_stop();
    }

    bool WebSocketClientState::is_connected() const
    {
        return state_.load() == WsState::CONNECTED;
    }

    WsState WebSocketClientState::state() const
    {
        return state_.load();
    }

    bool WebSocketClientState::send(const std::string &message)
    {
        return is_connected() && ws_.send(message).success;
    }

    void WebSocketClientState::run()
    {
        running_.store(true);
        while (!should_stop_.load())
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        running_.store(false);
    }

    void WebSocketClientState::stop()
    {
        const bool internal = is_internal_callback(this);
        should_stop_.store(true);
        disconnect();
        if (internal) return;
        for (int waits = 0; running_.load() && waits < 100; ++waits)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
