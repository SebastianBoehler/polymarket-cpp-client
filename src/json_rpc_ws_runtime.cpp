#include "json_rpc_ws_runtime.hpp"

namespace polymarket::detail
{
    std::shared_ptr<EvmJsonRpcWsRuntime> EvmJsonRpcWsRuntime::create(
        const std::string &rpc_ws_url)
    {
        auto runtime = std::shared_ptr<EvmJsonRpcWsRuntime>(
            new EvmJsonRpcWsRuntime(rpc_ws_url));
        runtime->bind_websocket_callbacks();
        return runtime;
    }

    EvmJsonRpcWsRuntime::EvmJsonRpcWsRuntime(
        const std::string &rpc_ws_url)
    {
        websocket_.set_url(rpc_ws_url);
        websocket_.set_auto_reconnect(true);
    }

    EvmJsonRpcWsRuntime::~EvmJsonRpcWsRuntime()
    {
        shutdown();
    }

    void EvmJsonRpcWsRuntime::bind_websocket_callbacks()
    {
        std::weak_ptr<EvmJsonRpcWsRuntime> weak = shared_from_this();
        websocket_.on_message(
            [weak](const std::string &message)
            {
                if (auto runtime = weak.lock())
                    runtime->handle_message(message);
            });
        websocket_.on_connect(
            [weak]
            {
                if (auto runtime = weak.lock()) runtime->resubscribe();
            });
        websocket_.on_error(
            [weak](const std::string &error)
            {
                if (auto runtime = weak.lock())
                {
                    const auto generation = runtime->stream_generation_.load();
                    runtime->emit_error(error, generation);
                }
            });
        websocket_.on_stream_gap(
            [weak](uint64_t websocket_generation)
            {
                if (websocket_generation <= 1) return;
                if (auto runtime = weak.lock())
                {
                    const auto generation = runtime->stream_generation_.load();
                    runtime->emit_error(
                        "EVM websocket stream gap detected at generation " +
                            std::to_string(websocket_generation),
                        generation);
                }
            });
    }

    void EvmJsonRpcWsRuntime::shutdown()
    {
        if (shutdown_started_.exchange(true)) return;
        owner_active_.store(false, std::memory_order_release);
        deactivate_stream();
        clear_callbacks();
        websocket_.stop();
    }

    void EvmJsonRpcWsRuntime::set_ping_interval_ms(int interval_ms)
    {
        if (owner_active_.load(std::memory_order_acquire))
            websocket_.set_ping_interval_ms(interval_ms);
    }

    void EvmJsonRpcWsRuntime::set_auto_reconnect(bool enabled)
    {
        if (owner_active_.load(std::memory_order_acquire))
            websocket_.set_auto_reconnect(enabled);
    }

    void EvmJsonRpcWsRuntime::on_log(EvmLogCallback callback)
    {
        update_callbacks([callback = std::move(callback)](auto &callbacks) mutable
                         { callbacks.log = std::move(callback); });
    }

    void EvmJsonRpcWsRuntime::on_pending_transaction(
        EvmPendingTxCallback callback)
    {
        update_callbacks([callback = std::move(callback)](auto &callbacks) mutable
                         { callbacks.pending_transaction = std::move(callback); });
    }

    void EvmJsonRpcWsRuntime::on_head(EvmJsonCallback callback)
    {
        update_callbacks([callback = std::move(callback)](auto &callbacks) mutable
                         { callbacks.head = std::move(callback); });
    }

    void EvmJsonRpcWsRuntime::on_error(EvmRpcErrorCallback callback)
    {
        update_callbacks([callback = std::move(callback)](auto &callbacks) mutable
                         { callbacks.error = std::move(callback); });
    }

    void EvmJsonRpcWsRuntime::clear_callbacks()
    {
        std::lock_guard lock(callback_update_mutex_);
        std::shared_ptr<const EvmJsonRpcWsCallbacks> empty =
            std::make_shared<EvmJsonRpcWsCallbacks>();
        std::atomic_store_explicit(&callbacks_, std::move(empty),
                                   std::memory_order_release);
    }

    bool EvmJsonRpcWsRuntime::connect()
    {
        if (!owner_active_.load(std::memory_order_acquire)) return false;
        stream_active_.store(true, std::memory_order_release);
        const bool connected = websocket_.connect();
        if (!connected) deactivate_stream();
        return connected;
    }

    void EvmJsonRpcWsRuntime::disconnect()
    {
        deactivate_stream();
        websocket_.disconnect();
    }

    bool EvmJsonRpcWsRuntime::is_connected() const
    {
        return owner_active_.load(std::memory_order_acquire) &&
               websocket_.is_connected();
    }

    void EvmJsonRpcWsRuntime::run()
    {
        if (owner_active_.load(std::memory_order_acquire)) websocket_.run();
    }

    void EvmJsonRpcWsRuntime::stop()
    {
        deactivate_stream();
        websocket_.stop();
    }

    void EvmJsonRpcWsRuntime::deactivate_stream()
    {
        if (stream_active_.exchange(false, std::memory_order_acq_rel))
            stream_generation_.fetch_add(1);
    }

    bool EvmJsonRpcWsRuntime::dispatch_is_current(
        uint64_t expected_generation) const
    {
        return owner_active_.load(std::memory_order_acquire) &&
               stream_active_.load(std::memory_order_acquire) &&
               expected_generation == stream_generation_.load();
    }

    bool EvmJsonRpcWsRuntime::subscribe_logs(const EvmLogFilter &filter)
    {
        if (!owner_active_.load(std::memory_order_acquire)) return false;
        Subscription subscription{SubscriptionType::Logs, filter};
        {
            std::lock_guard lock(subscriptions_mutex_);
            subscriptions_.push_back(subscription);
        }
        const auto generation = stream_generation_.load();
        return !is_connected() || send_subscription(subscription, generation);
    }

    bool EvmJsonRpcWsRuntime::subscribe_pending_transactions()
    {
        if (!owner_active_.load(std::memory_order_acquire)) return false;
        Subscription subscription{SubscriptionType::PendingTransactions, {}};
        {
            std::lock_guard lock(subscriptions_mutex_);
            subscriptions_.push_back(subscription);
        }
        const auto generation = stream_generation_.load();
        return !is_connected() || send_subscription(subscription, generation);
    }

    bool EvmJsonRpcWsRuntime::subscribe_new_heads()
    {
        if (!owner_active_.load(std::memory_order_acquire)) return false;
        Subscription subscription{SubscriptionType::NewHeads, {}};
        {
            std::lock_guard lock(subscriptions_mutex_);
            subscriptions_.push_back(subscription);
        }
        const auto generation = stream_generation_.load();
        return !is_connected() || send_subscription(subscription, generation);
    }
}
