#pragma once

#include <string>
#include <functional>
#include <atomic>
#include <thread>
#include <mutex>
#include <memory>
#include <ixwebsocket/IXWebSocket.h>

namespace polymarket
{

    // WebSocket connection state
    enum class WsState
    {
        DISCONNECTED,
        CONNECTING,
        CONNECTED,
        RECONNECTING,
        CLOSING,
        CLOSED
    };

    // Callbacks
    using OnMessageCallback = std::function<void(const std::string &)>;
    using OnConnectCallback = std::function<void()>;
    using OnDisconnectCallback = std::function<void()>;
    using OnErrorCallback = std::function<void(const std::string &)>;

    // High-performance WebSocket client using IXWebSocket
    class WebSocketClient
    {
    public:
        WebSocketClient();
        ~WebSocketClient();

        // Disable copy
        WebSocketClient(const WebSocketClient &) = delete;
        WebSocketClient &operator=(const WebSocketClient &) = delete;

        // Configuration
        void set_url(const std::string &url);
        void set_ping_interval_ms(int interval_ms);
        void set_auto_reconnect(bool enabled);

        // Callbacks
        void on_message(OnMessageCallback callback);
        void on_connect(OnConnectCallback callback);
        void on_disconnect(OnDisconnectCallback callback);
        void on_error(OnErrorCallback callback);

        // Connection
        bool connect();
        void disconnect();
        bool is_connected() const;
        WsState state() const;

        // Send message
        bool send(const std::string &message);

        // Run event loop (blocking) - IXWebSocket runs in its own thread
        void run();

        // Stop event loop
        void stop();

        // Get statistics
        uint64_t messages_received() const { return messages_received_.load(); }
        uint64_t bytes_received() const { return bytes_received_.load(); }

    private:
        ix::WebSocket ws_;

        // Configuration
        std::string url_;
        int ping_interval_ms_;
        bool auto_reconnect_;

        // State
        std::atomic<WsState> state_;
        std::atomic<bool> running_;
        std::atomic<bool> should_stop_;

        // Callbacks
        OnMessageCallback on_message_cb_;
        OnConnectCallback on_connect_cb_;
        OnDisconnectCallback on_disconnect_cb_;
        OnErrorCallback on_error_cb_;

        // Statistics
        std::atomic<uint64_t> messages_received_{0};
        std::atomic<uint64_t> bytes_received_{0};
    };

} // namespace polymarket
