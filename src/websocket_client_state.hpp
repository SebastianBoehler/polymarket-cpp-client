#pragma once

#include "websocket_client.hpp"

#include <atomic>
#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

#include <ixwebsocket/IXWebSocket.h>

namespace polymarket::detail
{
    class BoundedMessageQueue;

    struct WebSocketCallbacks
    {
        OnMessageCallback message;
        OnSequencedMessageCallback sequenced_message;
        OnTypedMessageCallback typed_message;
        OnConnectCallback connect;
        OnDisconnectCallback disconnect;
        OnErrorCallback error;
        OnStreamGapCallback stream_gap;
    };

    class WebSocketClientState final
        : public std::enable_shared_from_this<WebSocketClientState>
    {
    public:
        static std::shared_ptr<WebSocketClientState> create();
        ~WebSocketClientState();

        void set_url(const std::string &url);
        void set_ping_interval_ms(int interval_ms);
        void set_auto_reconnect(bool enabled);
        void configure(const WebSocketOptions &options);
        WebSocketOptions options() const;

        void on_message(OnMessageCallback callback);
        void on_sequenced_message(OnSequencedMessageCallback callback);
        void on_typed_message(OnTypedMessageCallback callback);
        void on_connect(OnConnectCallback callback);
        void on_disconnect(OnDisconnectCallback callback);
        void on_error(OnErrorCallback callback);
        void on_stream_gap(OnStreamGapCallback callback);

        bool connect();
        bool wait_until_connected(std::chrono::milliseconds timeout);
        void disconnect();
        bool is_connected() const;
        WsState state() const;
        bool send(const std::string &message);
        void run();
        void stop();

        void track_subscription(const std::string &message);
        void untrack_subscription(const std::string &message);
        void clear_subscriptions();
        void replace_subscriptions(std::vector<std::string> messages);
        std::size_t tracked_subscription_count() const;
        void request_resnapshot(const std::string &reason);

        uint64_t messages_received() const;
        uint64_t bytes_received() const;
        uint64_t stream_generation() const;
        WebSocketStats stats() const;

    private:
        WebSocketClientState();

        static void validate_options(const WebSocketOptions &options);
        void require_inactive_locked() const;
        void apply_options_locked();
        void install_transport_callback();
        void handle_transport_message(const ix::WebSocketMessagePtr &message);
        void handle_open();
        void handle_close();
        void handle_error(const ix::WebSocketMessagePtr &message);

        void start_message_worker();
        void request_message_worker_stop();
        void stop_message_worker();
        void start_transport_worker();
        bool request_transport_stop();
        void wait_for_transport_stop();
        void transport_worker_loop();
        void finish_disconnect();
        void publish_disconnected();

        void enqueue_message(const std::string &message);
        void dispatch_message(const std::string &message, uint64_t generation);
        void mark_stream_gap();
        bool restore_subscriptions();
        void report_callback_failure(const char *name, const char *reason) noexcept;
        bool invoke_error_callback(const std::string &message) noexcept;

        template <typename Update>
        void update_callbacks(Update &&update)
        {
            std::lock_guard<std::mutex> lock(callback_update_mutex_);
            auto next = std::make_shared<WebSocketCallbacks>(*callbacks_snapshot());
            update(*next);
            std::shared_ptr<const WebSocketCallbacks> immutable = std::move(next);
            std::atomic_store_explicit(&callbacks_, std::move(immutable),
                                       std::memory_order_release);
        }

        std::shared_ptr<const WebSocketCallbacks> callbacks_snapshot() const noexcept
        {
            return std::atomic_load_explicit(&callbacks_, std::memory_order_acquire);
        }

        template <typename Callback, typename... Args>
        bool invoke_user_callback(const char *name,
                                  const Callback &callback,
                                  Args &&...args) noexcept
        {
            if (!callback) return true;
            try
            {
                callback(std::forward<Args>(args)...);
                return true;
            }
            catch (const std::exception &error)
            {
                callback_errors_++;
                report_callback_failure(name, error.what());
            }
            catch (...)
            {
                callback_errors_++;
                report_callback_failure(name, "unknown exception");
            }
            return false;
        }

        ix::WebSocket ws_;
        std::unique_ptr<BoundedMessageQueue> message_queue_;
        WebSocketOptions options_;

        mutable std::mutex lifecycle_mutex_;
        mutable std::mutex callback_update_mutex_;
        mutable std::mutex subscriptions_mutex_;
        mutable std::mutex state_wait_mutex_;
        std::condition_variable state_cv_;

        std::atomic<WsState> state_{WsState::DISCONNECTED};
        std::atomic<bool> running_{false};
        std::atomic<bool> should_stop_{false};
        std::atomic<bool> worker_running_{false};
        std::atomic<bool> has_connected_{false};
        std::atomic<bool> resnapshot_pending_{false};
        std::atomic<bool> transport_stop_pending_{false};
        std::thread message_worker_;

        std::mutex transport_mutex_;
        std::condition_variable transport_cv_;
        bool transport_worker_active_{false};
        bool transport_stop_requested_{false};

        std::shared_ptr<const WebSocketCallbacks> callbacks_{
            std::make_shared<WebSocketCallbacks>()};
        std::vector<std::string> subscriptions_;

        std::atomic<uint64_t> messages_received_{0};
        std::atomic<uint64_t> bytes_received_{0};
        std::atomic<uint64_t> reconnects_{0};
        std::atomic<uint64_t> parse_errors_{0};
        std::atomic<uint64_t> callback_errors_{0};
        std::atomic<uint64_t> last_message_time_ns_{0};
        std::atomic<uint64_t> stream_generation_{0};
    };
}
