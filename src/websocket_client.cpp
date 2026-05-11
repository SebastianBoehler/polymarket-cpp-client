#include "websocket_client.hpp"
#include "types.hpp"
#include "websocket_resilience.hpp"
#include <iostream>

namespace polymarket
{

    WebSocketClient::WebSocketClient()
        : message_queue_(std::make_unique<detail::BoundedMessageQueue>(options_.message_queue_limit)),
          state_(WsState::DISCONNECTED),
          running_(false),
          should_stop_(false)
    {
        apply_options();
    }

    WebSocketClient::~WebSocketClient()
    {
        stop();
    }

    void WebSocketClient::set_url(const std::string &url)
    {
        url_ = url;
        ws_.setUrl(url);
    }

    void WebSocketClient::set_ping_interval_ms(int interval_ms)
    {
        options_.ping_interval_ms = interval_ms;
        apply_options();
    }

    void WebSocketClient::set_auto_reconnect(bool enabled)
    {
        options_.reconnect_enabled = enabled;
        apply_options();
    }

    void WebSocketClient::on_message(OnMessageCallback callback)
    {
        on_message_cb_ = std::move(callback);
    }

    void WebSocketClient::on_typed_message(OnTypedMessageCallback callback)
    {
        on_typed_message_cb_ = std::move(callback);
    }

    void WebSocketClient::on_connect(OnConnectCallback callback)
    {
        on_connect_cb_ = std::move(callback);
    }

    void WebSocketClient::on_disconnect(OnDisconnectCallback callback)
    {
        on_disconnect_cb_ = std::move(callback);
    }

    void WebSocketClient::on_error(OnErrorCallback callback)
    {
        on_error_cb_ = std::move(callback);
    }

    bool WebSocketClient::connect()
    {
        if (state_.load() == WsState::CONNECTED || state_.load() == WsState::CONNECTING)
        {
            return true;
        }

        // Set up message handler
        ws_.setOnMessageCallback([this](const ix::WebSocketMessagePtr &msg)
                                 {
            switch (msg->type)
            {
            case ix::WebSocketMessageType::Open:
                state_.store(WsState::CONNECTED);
                if (has_connected_.exchange(true))
                {
                    reconnects_++;
                }
                restore_subscriptions();
                if (on_connect_cb_)
                {
                    on_connect_cb_();
                }
                break;

            case ix::WebSocketMessageType::Close:
                state_.store(options_.reconnect_enabled ? WsState::RECONNECTING : WsState::DISCONNECTED);
                if (on_disconnect_cb_)
                {
                    on_disconnect_cb_();
                }
                break;

            case ix::WebSocketMessageType::Error:
                state_.store(options_.reconnect_enabled ? WsState::RECONNECTING : WsState::DISCONNECTED);
                if (on_error_cb_)
                {
                    on_error_cb_(msg->errorInfo.reason);
                }
                break;

            case ix::WebSocketMessageType::Message:
                messages_received_++;
                bytes_received_ += msg->str.size();
                last_message_time_ns_.store(now_ns());
                enqueue_message(msg->str);
                break;

            case ix::WebSocketMessageType::Ping:
            case ix::WebSocketMessageType::Pong:
            case ix::WebSocketMessageType::Fragment:
                // Handled internally by IXWebSocket
                break;
            } });

        state_.store(WsState::CONNECTING);
        start_message_worker();
        ws_.start();

        return true;
    }

    void WebSocketClient::disconnect()
    {
        state_.store(WsState::CLOSING);
        ws_.stop();
        stop_message_worker();
        state_.store(WsState::DISCONNECTED);
    }

    bool WebSocketClient::is_connected() const
    {
        return state_.load() == WsState::CONNECTED;
    }

    WsState WebSocketClient::state() const
    {
        return state_.load();
    }

    bool WebSocketClient::send(const std::string &message)
    {
        if (!is_connected())
        {
            return false;
        }

        auto result = ws_.send(message);
        return result.success;
    }

    void WebSocketClient::run()
    {
        running_.store(true);
        should_stop_.store(false);

        // IXWebSocket runs in its own thread, so we just wait here
        while (!should_stop_.load())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        running_.store(false);
    }

    void WebSocketClient::stop()
    {
        should_stop_.store(true);
        disconnect();

        // Wait for run loop to exit
        int wait_count = 0;
        while (running_.load() && wait_count < 100)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            wait_count++;
        }
    }

} // namespace polymarket
