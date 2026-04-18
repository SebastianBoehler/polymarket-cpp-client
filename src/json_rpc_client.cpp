#include "json_rpc_client.hpp"
#include <stdexcept>

namespace polymarket
{
    EvmJsonRpcHttpClient::EvmJsonRpcHttpClient(const std::string &rpc_url)
    {
        http_.set_base_url(rpc_url);
    }

    void EvmJsonRpcHttpClient::set_timeout_ms(long timeout_ms)
    {
        http_.set_timeout_ms(timeout_ms);
    }

    void EvmJsonRpcHttpClient::set_proxy(const std::string &proxy_url)
    {
        http_.set_proxy(proxy_url);
    }

    nlohmann::json EvmJsonRpcHttpClient::call(const std::string &method,
                                              const nlohmann::json &params)
    {
        nlohmann::json request = {
            {"jsonrpc", "2.0"},
            {"id", next_id_++},
            {"method", method},
            {"params", params}};

        auto response = http_.post("", request.dump());
        if (!response.ok())
        {
            throw std::runtime_error("JSON-RPC HTTP error: " + response.error +
                                     " status=" + std::to_string(response.status_code));
        }

        auto body = nlohmann::json::parse(response.body);
        if (body.contains("error"))
        {
            throw std::runtime_error("JSON-RPC error: " + body["error"].dump());
        }
        return body.value("result", nlohmann::json(nullptr));
    }

    std::string EvmJsonRpcHttpClient::block_number()
    {
        return call("eth_blockNumber", nlohmann::json::array()).get<std::string>();
    }

    std::vector<EvmLog> EvmJsonRpcHttpClient::get_logs(const EvmLogFilter &filter)
    {
        auto result = call("eth_getLogs", nlohmann::json::array({filter.to_json()}));
        std::vector<EvmLog> logs;
        if (!result.is_array())
            return logs;
        for (const auto &raw : result)
        {
            logs.push_back(evm_log_from_json(raw));
        }
        return logs;
    }

    nlohmann::json EvmJsonRpcHttpClient::get_transaction_by_hash(const std::string &tx_hash)
    {
        return call("eth_getTransactionByHash", nlohmann::json::array({tx_hash}));
    }

    EvmJsonRpcWsClient::EvmJsonRpcWsClient(const std::string &rpc_ws_url)
    {
        ws_.set_url(rpc_ws_url);
        ws_.set_auto_reconnect(true);
        ws_.on_message([this](const std::string &message) { handle_message(message); });
        ws_.on_connect([this]() { resubscribe(); });
        ws_.on_error([this](const std::string &error) { emit_error(error); });
    }

    EvmJsonRpcWsClient::~EvmJsonRpcWsClient()
    {
        stop();
    }

    void EvmJsonRpcWsClient::set_ping_interval_ms(int interval_ms)
    {
        ws_.set_ping_interval_ms(interval_ms);
    }

    void EvmJsonRpcWsClient::set_auto_reconnect(bool enabled)
    {
        ws_.set_auto_reconnect(enabled);
    }

    void EvmJsonRpcWsClient::on_log(EvmLogCallback callback)
    {
        log_cb_ = std::move(callback);
    }

    void EvmJsonRpcWsClient::on_pending_transaction(EvmPendingTxCallback callback)
    {
        pending_tx_cb_ = std::move(callback);
    }

    void EvmJsonRpcWsClient::on_head(EvmJsonCallback callback)
    {
        head_cb_ = std::move(callback);
    }

    void EvmJsonRpcWsClient::on_error(EvmRpcErrorCallback callback)
    {
        error_cb_ = std::move(callback);
    }

    bool EvmJsonRpcWsClient::connect()
    {
        return ws_.connect();
    }

    void EvmJsonRpcWsClient::disconnect()
    {
        ws_.disconnect();
    }

    bool EvmJsonRpcWsClient::is_connected() const
    {
        return ws_.is_connected();
    }

    void EvmJsonRpcWsClient::run()
    {
        ws_.run();
    }

    void EvmJsonRpcWsClient::stop()
    {
        ws_.stop();
    }

    bool EvmJsonRpcWsClient::subscribe_logs(const EvmLogFilter &filter)
    {
        Subscription subscription{SubscriptionType::Logs, filter};
        {
            std::lock_guard<std::mutex> lock(mutex_);
            subscriptions_.push_back(subscription);
        }
        return !is_connected() || send_subscription(subscription);
    }

    bool EvmJsonRpcWsClient::subscribe_pending_transactions()
    {
        Subscription subscription{SubscriptionType::PendingTransactions, {}};
        {
            std::lock_guard<std::mutex> lock(mutex_);
            subscriptions_.push_back(subscription);
        }
        return !is_connected() || send_subscription(subscription);
    }

    bool EvmJsonRpcWsClient::subscribe_new_heads()
    {
        Subscription subscription{SubscriptionType::NewHeads, {}};
        {
            std::lock_guard<std::mutex> lock(mutex_);
            subscriptions_.push_back(subscription);
        }
        return !is_connected() || send_subscription(subscription);
    }

    void EvmJsonRpcWsClient::handle_message(const std::string &message)
    {
        try
        {
            auto body = nlohmann::json::parse(message);
            if (body.contains("error"))
            {
                emit_error(body["error"].dump());
                return;
            }
            if (body.value("method", "") != "eth_subscription")
                return;
            if (!body.contains("params") || !body["params"].contains("result"))
            {
                emit_error("JSON-RPC subscription message missing params.result");
                return;
            }

            const auto &result = body["params"]["result"];
            if (result.is_string())
            {
                if (pending_tx_cb_)
                    pending_tx_cb_(result.get<std::string>());
                return;
            }
            if (!result.is_object())
                return;
            if (result.contains("topics") && result.contains("address"))
            {
                if (log_cb_)
                    log_cb_(evm_log_from_json(result));
                return;
            }
            if (head_cb_)
                head_cb_(result);
        }
        catch (const std::exception &error)
        {
            emit_error(std::string("invalid JSON-RPC WS message: ") + error.what());
        }
    }

    void EvmJsonRpcWsClient::resubscribe()
    {
        std::vector<Subscription> copy;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            copy = subscriptions_;
        }
        for (const auto &subscription : copy)
        {
            send_subscription(subscription);
        }
    }

    bool EvmJsonRpcWsClient::send_subscription(const Subscription &subscription)
    {
        nlohmann::json request = {
            {"jsonrpc", "2.0"},
            {"id", next_id_.fetch_add(1)},
            {"method", "eth_subscribe"},
            {"params", subscription_params(subscription)}};
        return ws_.send(request.dump());
    }

    nlohmann::json EvmJsonRpcWsClient::subscription_params(const Subscription &subscription) const
    {
        if (subscription.type == SubscriptionType::Logs)
        {
            return nlohmann::json::array({"logs", subscription.filter.to_json()});
        }
        if (subscription.type == SubscriptionType::PendingTransactions)
        {
            return nlohmann::json::array({"newPendingTransactions"});
        }
        return nlohmann::json::array({"newHeads"});
    }

    void EvmJsonRpcWsClient::emit_error(const std::string &message)
    {
        if (error_cb_)
            error_cb_(message);
    }

} // namespace polymarket
