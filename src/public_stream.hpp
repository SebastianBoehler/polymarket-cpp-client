#pragma once

#include "polymarket/websocket_client.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// A public WebSocket feed shared by SportsStream and RtdsStream: it owns the
// connection, reports gaps and recovery, and hands each current message to
// the owner's handler on the WebSocket message thread.
namespace polymarket::detail
{
    struct PublicStreamCallbacks
    {
        std::function<void()> gap;
        std::function<void()> recovered;
        std::function<void(const std::string &)> error;
    };

    class PublicStream final : public std::enable_shared_from_this<PublicStream>
    {
      public:
        using MessageHandler = std::function<void(const std::string &message)>;

        static std::shared_ptr<PublicStream> create(const std::string &url,
                                                    const WebSocketOptions &options,
                                                    std::chrono::milliseconds connect_timeout,
                                                    MessageHandler handler);
        ~PublicStream();

        PublicStream(const PublicStream &) = delete;
        PublicStream &operator=(const PublicStream &) = delete;

        void shutdown();
        void on_stream_gap(std::function<void()> callback);
        void on_stream_recovered(std::function<void()> callback);
        void on_error(std::function<void(const std::string &)> callback);

        bool connect();
        void disconnect();
        bool is_connected() const;
        void run();
        void stop();

        // Subscription frames sent again after every reconnect.
        void replace_subscriptions(std::vector<std::string> messages);
        bool send(const std::string &message);
        void request_resnapshot(const std::string &reason);

      private:
        PublicStream(const std::string &url, const WebSocketOptions &options,
                     std::chrono::milliseconds connect_timeout, MessageHandler handler);
        void bind_websocket_callbacks();
        void handle_message(const std::string &message, std::uint64_t generation);
        void handle_gap(std::uint64_t generation);
        void handle_connect();
        void handle_error(const std::string &error);
        bool active() const;

        template <typename Update> void update_callbacks(Update &&update)
        {
            std::lock_guard lock(callback_update_mutex_);
            if (!owner_active_.load(std::memory_order_acquire)) return;
            auto next = std::make_shared<PublicStreamCallbacks>(*callbacks_snapshot());
            update(*next);
            std::shared_ptr<const PublicStreamCallbacks> immutable = std::move(next);
            std::atomic_store_explicit(&callbacks_, std::move(immutable),
                                       std::memory_order_release);
        }

        std::shared_ptr<const PublicStreamCallbacks> callbacks_snapshot() const
        {
            return std::atomic_load_explicit(&callbacks_, std::memory_order_acquire);
        }

        WebSocketClient websocket_;
        std::chrono::milliseconds connect_timeout_;
        MessageHandler handler_;

        mutable std::mutex callback_update_mutex_;
        std::shared_ptr<const PublicStreamCallbacks> callbacks_{
            std::make_shared<PublicStreamCallbacks>()};

        std::atomic<bool> owner_active_{true};
        std::atomic<bool> stream_active_{false};
        std::atomic<bool> shutdown_started_{false};
        // WebSocket generation of the gap awaiting recovery; 0 when none.
        std::atomic<std::uint64_t> recovery_generation_{0};
    };
} // namespace polymarket::detail
