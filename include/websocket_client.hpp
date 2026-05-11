#pragma once

#include <string>
#include <functional>
#include <atomic>
#include <thread>
#include <mutex>
#include <memory>
#include <vector>
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

    struct WebSocketOptions
    {
        bool reconnect_enabled{true};
        int max_reconnect_attempts{0};
        uint32_t min_backoff_ms{250};
        uint32_t max_backoff_ms{10000};
        int ping_interval_ms{5000};
        std::size_t message_queue_limit{1024};
    };

    struct WebSocketStats
    {
        uint64_t messages_received{0};
        uint64_t bytes_received{0};
        uint64_t reconnects{0};
        uint64_t dropped_messages{0};
        uint64_t parse_errors{0};
        uint64_t last_message_time_ns{0};
    };

    struct TypedWebSocketMessage
    {
        std::string topic;
        std::string type;
        std::string event_type;
        std::string asset_id;
        std::string payload;
        std::string raw;
    };

    using OnTypedMessageCallback = std::function<void(const TypedWebSocketMessage &)>;

    namespace detail
    {
        class BoundedMessageQueue;
    }

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
        void configure(const WebSocketOptions &options);
        WebSocketOptions options() const;

        // Callbacks
        void on_message(OnMessageCallback callback);
        void on_typed_message(OnTypedMessageCallback callback);
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
        void track_subscription(const std::string &message);
        void untrack_subscription(const std::string &message);
        void clear_subscriptions();
        std::size_t tracked_subscription_count() const;

        // Run event loop (blocking) - IXWebSocket runs in its own thread
        void run();

        // Stop event loop
        void stop();

        // Get statistics
        uint64_t messages_received() const { return messages_received_.load(); }
        uint64_t bytes_received() const { return bytes_received_.load(); }
        WebSocketStats stats() const;

    private:
        ix::WebSocket ws_;
        std::unique_ptr<detail::BoundedMessageQueue> message_queue_;

        // Configuration
        std::string url_;
        WebSocketOptions options_;

        // State
        std::atomic<WsState> state_;
        std::atomic<bool> running_;
        std::atomic<bool> should_stop_;
        std::atomic<bool> worker_running_{false};
        std::atomic<bool> has_connected_{false};
        std::thread message_worker_;

        // Callbacks
        OnMessageCallback on_message_cb_;
        OnTypedMessageCallback on_typed_message_cb_;
        OnConnectCallback on_connect_cb_;
        OnDisconnectCallback on_disconnect_cb_;
        OnErrorCallback on_error_cb_;

        mutable std::mutex subscriptions_mutex_;
        std::vector<std::string> subscriptions_;

        // Statistics
        std::atomic<uint64_t> messages_received_{0};
        std::atomic<uint64_t> bytes_received_{0};
        std::atomic<uint64_t> reconnects_{0};
        std::atomic<uint64_t> parse_errors_{0};
        std::atomic<uint64_t> last_message_time_ns_{0};

        void apply_options();
        void start_message_worker();
        void stop_message_worker();
        void enqueue_message(const std::string &message);
        void dispatch_message(const std::string &message);
        void restore_subscriptions();
    };

} // namespace polymarket
