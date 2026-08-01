#include "json_rpc_ws_runtime.hpp"

#include <stdexcept>

namespace polymarket::detail
{
    void EvmJsonRpcWsRuntime::handle_message(const std::string &message)
    {
        const auto generation = stream_generation_.load();
        if (!dispatch_is_current(generation)) return;
        try
        {
            const auto body = nlohmann::json::parse(message);
            if (body.contains("error"))
            {
                emit_error(body["error"].dump(), generation);
                return;
            }
            if (body.value("method", "") != "eth_subscription") return;
            if (!body.contains("params") ||
                !body["params"].contains("result"))
            {
                emit_error(
                    "JSON-RPC subscription message missing params.result",
                    generation);
                return;
            }

            const auto &result = body["params"]["result"];
            const auto callbacks = callbacks_snapshot();
            if (result.is_string())
            {
                if (callbacks->pending_transaction &&
                    dispatch_is_current(generation))
                    callbacks->pending_transaction(result.get<std::string>());
                return;
            }
            if (!result.is_object()) return;
            if (result.contains("topics") && result.contains("address"))
            {
                if (callbacks->log && dispatch_is_current(generation))
                    callbacks->log(evm_log_from_json(result));
                return;
            }
            if (callbacks->head && dispatch_is_current(generation))
                callbacks->head(result);
        }
        catch (const std::exception &error)
        {
            if (!dispatch_is_current(generation)) return;
            emit_error(std::string("invalid JSON-RPC WS message: ") +
                           error.what(),
                       generation);
        }
    }

    void EvmJsonRpcWsRuntime::emit_error(
        const std::string &message,
        uint64_t expected_generation)
    {
        if (!dispatch_is_current(expected_generation)) return;
        const auto callbacks = callbacks_snapshot();
        if (callbacks->error && dispatch_is_current(expected_generation))
            callbacks->error(message);
    }

    void EvmJsonRpcWsRuntime::resubscribe()
    {
        const auto generation = stream_generation_.load();
        if (!dispatch_is_current(generation)) return;
        std::vector<Subscription> subscriptions;
        {
            std::lock_guard lock(subscriptions_mutex_);
            subscriptions = subscriptions_;
        }
        for (const auto &subscription : subscriptions)
        {
            if (!send_subscription(subscription, generation)) return;
        }
    }

    bool EvmJsonRpcWsRuntime::send_subscription(
        const Subscription &subscription,
        uint64_t expected_generation)
    {
        if (!dispatch_is_current(expected_generation)) return false;
        nlohmann::json request = {
            {"jsonrpc", "2.0"},
            {"id", next_id_.fetch_add(1)},
            {"method", "eth_subscribe"},
            {"params", subscription_params(subscription)}};
        return dispatch_is_current(expected_generation) &&
               websocket_.send(request.dump());
    }

    nlohmann::json EvmJsonRpcWsRuntime::subscription_params(
        const Subscription &subscription)
    {
        if (subscription.type == SubscriptionType::Logs)
            return nlohmann::json::array(
                {"logs", subscription.filter.to_json()});
        if (subscription.type == SubscriptionType::PendingTransactions)
            return nlohmann::json::array({"newPendingTransactions"});
        return nlohmann::json::array({"newHeads"});
    }
}
