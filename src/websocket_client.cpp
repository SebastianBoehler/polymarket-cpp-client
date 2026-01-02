#include "websocket_client.hpp"
#include "types.hpp"
#include <iostream>

namespace polymarket
{

    WebSocketClient::WebSocketClient()
        : ping_interval_ms_(5000), auto_reconnect_(true), state_(WsState::DISCONNECTED), running_(false), should_stop_(false)
    {
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
        ping_interval_ms_ = interval_ms;
        ws_.setPingInterval(interval_ms);
    }

    void WebSocketClient::set_auto_reconnect(bool enabled)
    {
        auto_reconnect_ = enabled;
        ws_.enableAutomaticReconnection();
        if (!enabled)
        {
            ws_.disableAutomaticReconnection();
        }
    }

    void WebSocketClient::on_message(OnMessageCallback callback)
    {
        on_message_cb_ = std::move(callback);
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
                if (on_connect_cb_)
                {
                    on_connect_cb_();
                }
                break;

            case ix::WebSocketMessageType::Close:
                state_.store(WsState::DISCONNECTED);
                if (on_disconnect_cb_)
                {
                    on_disconnect_cb_();
                }
                break;

            case ix::WebSocketMessageType::Error:
                state_.store(WsState::DISCONNECTED);
                if (on_error_cb_)
                {
                    on_error_cb_(msg->errorInfo.reason);
                }
                break;

            case ix::WebSocketMessageType::Message:
                messages_received_++;
                bytes_received_ += msg->str.size();
                if (on_message_cb_)
                {
                    on_message_cb_(msg->str);
                }
                break;

            case ix::WebSocketMessageType::Ping:
            case ix::WebSocketMessageType::Pong:
            case ix::WebSocketMessageType::Fragment:
                // Handled internally by IXWebSocket
                break;
            } });

        state_.store(WsState::CONNECTING);
        ws_.start();

        return true;
    }

    void WebSocketClient::disconnect()
    {
        state_.store(WsState::CLOSING);
        ws_.stop();
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
