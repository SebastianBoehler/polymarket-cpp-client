#pragma once

#include "user_stream.hpp"
#include "user_stream_protocol.hpp"
#include "websocket_client.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace polymarket::detail
{
    struct UserStreamCallbacks
    {
        UserOrderCallback order;
        UserTradeCallback trade;
        UserStreamGapCallback gap;
        UserStreamRecoveredCallback recovered;
        UserStreamErrorCallback error;
    };

    class UserStreamRuntime final
        : public std::enable_shared_from_this<UserStreamRuntime>
    {
    public:
        static std::shared_ptr<UserStreamRuntime> create(const Config &config,
                                                         const ApiCredentials &credentials);
        ~UserStreamRuntime();

        void shutdown();
        void subscribe_all_markets();
        void subscribe(const std::vector<std::string> &condition_ids);
        void unsubscribe(const std::vector<std::string> &condition_ids);
        void unsubscribe_all();
        bool is_subscribed_to_all_markets() const;
        std::vector<std::string> subscribed_markets() const;

        void on_order(UserOrderCallback callback);
        void on_trade(UserTradeCallback callback);
        void on_stream_gap(UserStreamGapCallback callback);
        void on_stream_recovered(UserStreamRecoveredCallback callback);
        void on_error(UserStreamErrorCallback callback);

        bool connect();
        void disconnect();
        bool is_connected() const;
        bool authentication_failed() const;
        void run();
        void stop();
        uint64_t order_events() const;
        uint64_t trade_events() const;

    private:
        UserStreamRuntime(const Config &config, const ApiCredentials &credentials);
        void bind_websocket_callbacks();

        void handle_message(const std::string &message,
                            uint64_t websocket_generation);
        void handle_stream_gap(uint64_t websocket_generation);
        void handle_connect();
        void handle_close(uint16_t code, const std::string &reason);
        void deactivate_stream();
        bool stream_is_current(uint64_t user_generation,
                               uint64_t websocket_generation) const;

        template <typename Update>
        void update_subscription(Update &&update)
        {
            if (!owner_active_.load(std::memory_order_acquire)) return;
            bool recover = false;
            bool reconnect = false;
            {
                std::lock_guard lock(subscriptions_mutex_);
                const auto before = subscription_;
                update(subscription_);
                if (before.all_markets == subscription_.all_markets &&
                    before.markets == subscription_.markets)
                    return;
                websocket_.replace_subscriptions(tracked_messages());
                if (!websocket_.is_connected()) return;
                const auto sync = plan_user_subscription_sync(
                    before, subscription_, credentials_);
                reconnect = sync.reconnect;
                for (const auto &message : sync.messages)
                {
                    if (!websocket_.send(message))
                    {
                        recover = true;
                        break;
                    }
                }
            }
            // Recovery fires the gap callback, so it must run unlocked.
            if (reconnect)
                websocket_.request_resnapshot(
                    "user subscription removed; reconnect required");
            else if (recover)
                websocket_.request_resnapshot(
                    "user subscription update send failed; reconnect required");
        }

        std::vector<std::string> tracked_messages() const;

        template <typename Update>
        void update_callbacks(Update &&update)
        {
            std::lock_guard lock(callback_update_mutex_);
            if (!owner_active_.load(std::memory_order_acquire)) return;
            auto next = std::make_shared<UserStreamCallbacks>(*callbacks_snapshot());
            update(*next);
            std::shared_ptr<const UserStreamCallbacks> immutable = std::move(next);
            std::atomic_store_explicit(&callbacks_, std::move(immutable),
                                       std::memory_order_release);
        }

        std::shared_ptr<const UserStreamCallbacks> callbacks_snapshot() const
        {
            return std::atomic_load_explicit(&callbacks_, std::memory_order_acquire);
        }

        void clear_callbacks();

        Config config_;
        ApiCredentials credentials_;
        WebSocketClient websocket_;

        mutable std::mutex subscriptions_mutex_;
        UserSubscriptionState subscription_;

        mutable std::mutex callback_update_mutex_;
        std::shared_ptr<const UserStreamCallbacks> callbacks_{
            std::make_shared<UserStreamCallbacks>()};

        std::atomic<bool> owner_active_{true};
        std::atomic<bool> stream_active_{false};
        std::atomic<bool> shutdown_started_{false};
        std::atomic<bool> authentication_failed_{false};
        std::atomic<uint64_t> order_events_{0};
        std::atomic<uint64_t> trade_events_{0};
        std::atomic<uint64_t> stream_generation_{0};
        // WebSocket generation of the gap awaiting recovery; 0 when none.
        std::atomic<uint64_t> recovery_generation_{0};
    };
}
