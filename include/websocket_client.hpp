#pragma once

#include <string>
#include <functional>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

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
    using OnSequencedMessageCallback = std::function<void(const std::string &, uint64_t)>;
    using OnStreamGapCallback = std::function<void(uint64_t)>;

    struct WebSocketOptions
    {
        bool reconnect_enabled{true};
        int max_reconnect_attempts{0};
        uint32_t min_backoff_ms{250};
        uint32_t max_backoff_ms{10000};
        int ping_interval_ms{10000};
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
        uint64_t callback_errors{0};
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
        class WebSocketClientState;
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
        void on_sequenced_message(OnSequencedMessageCallback callback);
        void on_typed_message(OnTypedMessageCallback callback);
        void on_connect(OnConnectCallback callback);
        void on_disconnect(OnDisconnectCallback callback);
        void on_error(OnErrorCallback callback);
        void on_stream_gap(OnStreamGapCallback callback);

        // Connection
        bool connect();
        bool wait_until_connected(std::chrono::milliseconds timeout);
        void disconnect();
        bool is_connected() const;
        WsState state() const;

        // Send message
        bool send(const std::string &message);
        void track_subscription(const std::string &message);
        void untrack_subscription(const std::string &message);
        void clear_subscriptions();
        void replace_subscriptions(std::vector<std::string> messages);
        std::size_t tracked_subscription_count() const;
        void request_resnapshot(const std::string &reason);

        // Run event loop (blocking) - IXWebSocket runs in its own thread
        void run();

        // Stop event loop
        void stop();

        // Get statistics
        uint64_t messages_received() const;
        uint64_t bytes_received() const;
        uint64_t stream_generation() const;
        WebSocketStats stats() const;

    private:
        std::shared_ptr<detail::WebSocketClientState> state_;
    };

} // namespace polymarket
