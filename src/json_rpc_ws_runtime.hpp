#pragma once

#include "json_rpc_client.hpp"
#include "websocket_client.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace polymarket::detail
{
    struct EvmJsonRpcWsCallbacks
    {
        EvmLogCallback log;
        EvmPendingTxCallback pending_transaction;
        EvmJsonCallback head;
        EvmRpcErrorCallback error;
    };

    class EvmJsonRpcWsRuntime final
        : public std::enable_shared_from_this<EvmJsonRpcWsRuntime>
    {
    public:
        static std::shared_ptr<EvmJsonRpcWsRuntime> create(
            const std::string &rpc_ws_url);
        ~EvmJsonRpcWsRuntime();

        void shutdown();
        void set_ping_interval_ms(int interval_ms);
        void set_auto_reconnect(bool enabled);
        void on_log(EvmLogCallback callback);
        void on_pending_transaction(EvmPendingTxCallback callback);
        void on_head(EvmJsonCallback callback);
        void on_error(EvmRpcErrorCallback callback);

        bool connect();
        void disconnect();
        bool is_connected() const;
        void run();
        void stop();

        bool subscribe_logs(const EvmLogFilter &filter);
        bool subscribe_pending_transactions();
        bool subscribe_new_heads();

    private:
        enum class SubscriptionType
        {
            Logs,
            PendingTransactions,
            NewHeads
        };

        struct Subscription
        {
            SubscriptionType type;
            EvmLogFilter filter;
        };

        explicit EvmJsonRpcWsRuntime(const std::string &rpc_ws_url);
        void bind_websocket_callbacks();
        void handle_message(const std::string &message);
        void resubscribe();
        bool send_subscription(const Subscription &subscription,
                               uint64_t expected_generation);
        static nlohmann::json subscription_params(
            const Subscription &subscription);
        void emit_error(const std::string &message,
                        uint64_t expected_generation);
        bool dispatch_is_current(uint64_t expected_generation) const;
        void deactivate_stream();

        template <typename Update>
        void update_callbacks(Update &&update)
        {
            std::lock_guard lock(callback_update_mutex_);
            if (!owner_active_.load(std::memory_order_acquire)) return;
            auto next = std::make_shared<EvmJsonRpcWsCallbacks>(
                *callbacks_snapshot());
            update(*next);
            std::shared_ptr<const EvmJsonRpcWsCallbacks> immutable =
                std::move(next);
            std::atomic_store_explicit(&callbacks_, std::move(immutable),
                                       std::memory_order_release);
        }

        std::shared_ptr<const EvmJsonRpcWsCallbacks> callbacks_snapshot() const
        {
            return std::atomic_load_explicit(&callbacks_,
                                             std::memory_order_acquire);
        }

        void clear_callbacks();

        WebSocketClient websocket_;
        mutable std::mutex subscriptions_mutex_;
        std::vector<Subscription> subscriptions_;
        std::atomic<uint64_t> next_id_{1};

        mutable std::mutex callback_update_mutex_;
        std::shared_ptr<const EvmJsonRpcWsCallbacks> callbacks_{
            std::make_shared<EvmJsonRpcWsCallbacks>()};

        std::atomic<bool> owner_active_{true};
        std::atomic<bool> stream_active_{false};
        std::atomic<bool> shutdown_started_{false};
        std::atomic<uint64_t> stream_generation_{0};
    };
}
