#include "websocket_callback_context.hpp"
#include "websocket_client_state.hpp"
#include "websocket_resilience.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace polymarket::detail
{
    void WebSocketClientState::configure(const WebSocketOptions &options)
    {
        std::lock_guard<std::mutex> lock(lifecycle_mutex_);
        require_inactive_locked();
        validate_options(options);
        options_ = options;
        apply_options_locked();
        message_queue_->reset(options_.message_queue_limit);
    }

    void WebSocketClientState::validate_options(const WebSocketOptions &options)
    {
        if (options.max_reconnect_attempts < 0)
            throw std::invalid_argument("max reconnect attempts cannot be negative");
        if (options.min_backoff_ms == 0 || options.max_backoff_ms == 0 ||
            options.min_backoff_ms > options.max_backoff_ms)
            throw std::invalid_argument("WebSocket reconnect backoff range is invalid");
        if (options.ping_interval_ms <= 0)
            throw std::invalid_argument("WebSocket ping interval must be positive");
        if (options.message_queue_limit == 0)
            throw std::invalid_argument("WebSocket message queue limit must be positive");
    }

    void WebSocketClientState::require_inactive_locked() const
    {
        const auto current = state_.load();
        if (current == WsState::CONNECTING || current == WsState::CONNECTED ||
            current == WsState::RECONNECTING || current == WsState::CLOSING)
        {
            throw std::logic_error("cannot reconfigure an active WebSocket client");
        }
    }

    WebSocketOptions WebSocketClientState::options() const
    {
        std::lock_guard<std::mutex> lock(lifecycle_mutex_);
        return options_;
    }

    void WebSocketClientState::set_ping_interval_ms(int interval_ms)
    {
        std::lock_guard<std::mutex> lock(lifecycle_mutex_);
        require_inactive_locked();
        if (interval_ms <= 0)
            throw std::invalid_argument("WebSocket ping interval must be positive");
        options_.ping_interval_ms = interval_ms;
        apply_options_locked();
    }

    void WebSocketClientState::set_auto_reconnect(bool enabled)
    {
        std::lock_guard<std::mutex> lock(lifecycle_mutex_);
        require_inactive_locked();
        options_.reconnect_enabled = enabled;
        apply_options_locked();
    }

    void WebSocketClientState::apply_options_locked()
    {
        const int ping_seconds = static_cast<int>(
            (static_cast<int64_t>(options_.ping_interval_ms) + 999) / 1000);
        ws_.setPingMessage("PING", ix::SendMessageKind::Text);
        ws_.setPingInterval(ping_seconds);
        ws_.setMinWaitBetweenReconnectionRetries(options_.min_backoff_ms);
        ws_.setMaxWaitBetweenReconnectionRetries(options_.max_backoff_ms);
        if (options_.reconnect_enabled)
        {
            ws_.enableAutomaticReconnection();
        }
        else
        {
            ws_.disableAutomaticReconnection();
        }
    }

    void WebSocketClientState::track_subscription(const std::string &message)
    {
        std::lock_guard<std::mutex> lock(subscriptions_mutex_);
        if (std::find(subscriptions_.begin(), subscriptions_.end(), message) == subscriptions_.end())
        {
            subscriptions_.push_back(message);
        }
    }

    void WebSocketClientState::untrack_subscription(const std::string &message)
    {
        std::lock_guard<std::mutex> lock(subscriptions_mutex_);
        subscriptions_.erase(std::remove(subscriptions_.begin(), subscriptions_.end(), message),
                             subscriptions_.end());
    }

    void WebSocketClientState::clear_subscriptions()
    {
        std::lock_guard<std::mutex> lock(subscriptions_mutex_);
        subscriptions_.clear();
    }

    void WebSocketClientState::replace_subscriptions(std::vector<std::string> messages)
    {
        std::lock_guard<std::mutex> lock(subscriptions_mutex_);
        subscriptions_ = std::move(messages);
    }

    std::size_t WebSocketClientState::tracked_subscription_count() const
    {
        std::lock_guard<std::mutex> lock(subscriptions_mutex_);
        return subscriptions_.size();
    }

    uint64_t WebSocketClientState::messages_received() const
    {
        return messages_received_.load();
    }

    uint64_t WebSocketClientState::bytes_received() const
    {
        return bytes_received_.load();
    }

    uint64_t WebSocketClientState::stream_generation() const
    {
        return stream_generation_.load();
    }

    WebSocketStats WebSocketClientState::stats() const
    {
        return {messages_received_.load(),
                bytes_received_.load(),
                reconnects_.load(),
                message_queue_->dropped(),
                parse_errors_.load(),
                last_message_time_ns_.load(),
                callback_errors_.load()};
    }

    void WebSocketClientState::start_message_worker()
    {
        if (worker_running_.load())
        {
            return;
        }
        if (message_worker_.joinable())
        {
            message_worker_.join();
            message_queue_->reset(options_.message_queue_limit);
        }

        worker_running_.store(true);
        auto self = shared_from_this();
        message_worker_ = std::thread([self]
                                      {
            while (self->worker_running_.load())
            {
                auto message = self->message_queue_->pop();
                if (!message) break;
                self->dispatch_message(message->payload, message->generation);
            } });
    }

    void WebSocketClientState::request_message_worker_stop()
    {
        message_queue_->close();
        worker_running_.store(false);
    }

    void WebSocketClientState::stop_message_worker()
    {
        request_message_worker_stop();
        if (message_worker_.joinable())
        {
            message_worker_.join();
        }
        std::lock_guard<std::mutex> lock(lifecycle_mutex_);
        message_queue_->reset(options_.message_queue_limit);
    }

    void WebSocketClientState::enqueue_message(const std::string &message)
    {
        if (resnapshot_pending_.load())
        {
            message_queue_->record_drop();
            return;
        }
        const auto result = message_queue_->push({message, stream_generation_.load()});
        if (result == QueuePushResult::Gap)
        {
            request_resnapshot("message queue overflow; resnapshot required");
        }
    }

    void WebSocketClientState::request_resnapshot(const std::string &reason)
    {
        if (transport_stop_pending_.load()) return;
        if (resnapshot_pending_.exchange(true)) return;
        mark_stream_gap();
        if (transport_stop_pending_.load()) return;

        std::lock_guard<std::mutex> lock(lifecycle_mutex_);
        if (transport_stop_pending_.load()) return;
        if (!options_.reconnect_enabled) ws_.enableAutomaticReconnection();
        ws_.close(ix::WebSocketCloseConstants::kInternalErrorCode, reason);
    }

    void WebSocketClientState::dispatch_message(const std::string &message,
                                                uint64_t generation)
    {
        InternalCallbackScope callback_scope(this);
        if (generation != stream_generation_.load()) return;

        auto callbacks = callbacks_snapshot();
        if (!invoke_user_callback("message", callbacks->message, message))
        {
            if (!transport_stop_pending_.load())
                request_resnapshot("message callback failed; resnapshot required");
            return;
        }
        if (!worker_running_.load() || transport_stop_pending_.load() ||
            generation != stream_generation_.load()) return;
        if (!invoke_user_callback("sequenced message", callbacks->sequenced_message,
                                  message, generation))
        {
            if (!transport_stop_pending_.load())
                request_resnapshot("sequenced message callback failed; resnapshot required");
            return;
        }
        if (!worker_running_.load() || transport_stop_pending_.load() ||
            generation != stream_generation_.load()) return;

        if (!callbacks->typed_message) return;
        std::optional<TypedWebSocketMessage> typed;
        try
        {
            typed = parse_typed_message(message);
        }
        catch (...)
        {
            parse_errors_++;
            return;
        }
        if (typed && !invoke_user_callback("typed message", callbacks->typed_message, *typed))
        {
            if (!transport_stop_pending_.load())
                request_resnapshot("typed message callback failed; resnapshot required");
        }
    }

    void WebSocketClientState::mark_stream_gap()
    {
        if (transport_stop_pending_.load()) return;
        const auto generation = stream_generation_.fetch_add(1) + 1;
        message_queue_->discard_pending();
        if (transport_stop_pending_.load()) return;
        auto callbacks = callbacks_snapshot();
        if (callbacks->stream_gap)
        {
            InternalCallbackScope callback_scope(this);
            invoke_user_callback("stream gap", callbacks->stream_gap, generation);
        }
    }

    bool WebSocketClientState::restore_subscriptions()
    {
        std::vector<std::string> subscriptions;
        {
            std::lock_guard<std::mutex> lock(subscriptions_mutex_);
            subscriptions = subscriptions_;
        }
        for (const auto &message : subscriptions)
        {
            if (!ws_.send(message).success)
            {
                request_resnapshot("failed to restore WebSocket subscriptions");
                return false;
            }
        }
        return true;
    }
}
