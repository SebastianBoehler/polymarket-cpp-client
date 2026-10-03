#include "json_rpc_client.hpp"
#include "json_rpc_ws_runtime.hpp"

#include <stdexcept>

namespace polymarket
{
    namespace
    {
        [[noreturn]] void invalid_json_rpc_response(const std::string &reason)
        {
            throw std::runtime_error("invalid JSON-RPC response: " + reason);
        }

        nlohmann::json json_rpc_result(const std::string &text, uint64_t request_id)
        {
            nlohmann::json body;
            try
            {
                body = nlohmann::json::parse(text);
            }
            catch (const nlohmann::json::parse_error &)
            {
                invalid_json_rpc_response("body is not valid JSON");
            }
            if (!body.is_object())
                invalid_json_rpc_response("body must be an object");
            if (!body.contains("jsonrpc") || !body.at("jsonrpc").is_string() ||
                body.at("jsonrpc") != "2.0")
                invalid_json_rpc_response("jsonrpc must be \"2.0\"");
            if (!body.contains("id") || !body.at("id").is_number_unsigned() ||
                body.at("id").get<uint64_t>() != request_id)
                invalid_json_rpc_response("id does not match the request");

            const bool has_result = body.contains("result");
            const bool has_error = body.contains("error");
            if (has_result == has_error)
                invalid_json_rpc_response("exactly one of result or error is required");
            if (has_error)
            {
                const auto &error = body.at("error");
                if (!error.is_object() || !error.contains("code") ||
                    !error.at("code").is_number_integer() ||
                    !error.contains("message") || !error.at("message").is_string())
                    invalid_json_rpc_response("error must contain an integer code and string message");
                throw std::runtime_error("JSON-RPC error: " + error.dump());
            }
            return body.at("result");
        }
    }

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

    nlohmann::json EvmJsonRpcHttpClient::call(
        const std::string &method,
        const nlohmann::json &params)
    {
        const auto request_id = next_id_.fetch_add(1, std::memory_order_relaxed);
        nlohmann::json request = {
            {"jsonrpc", "2.0"},
            {"id", request_id},
            {"method", method},
            {"params", params}};
        auto response = http_.post("", request.dump());
        if (!response.ok())
        {
            throw std::runtime_error("JSON-RPC HTTP error: " + response.error +
                                     " status=" +
                                     std::to_string(response.status_code));
        }
        return json_rpc_result(response.body, request_id);
    }

    std::string EvmJsonRpcHttpClient::block_number()
    {
        return call("eth_blockNumber", nlohmann::json::array()).get<std::string>();
    }

    std::vector<EvmLog> EvmJsonRpcHttpClient::get_logs(
        const EvmLogFilter &filter)
    {
        auto result = call("eth_getLogs",
                           nlohmann::json::array({filter.to_json()}));
        std::vector<EvmLog> logs;
        if (!result.is_array())
            throw std::runtime_error("invalid JSON-RPC result: eth_getLogs must return an array");
        for (const auto &raw : result)
            logs.push_back(evm_log_from_json(raw));
        return logs;
    }

    nlohmann::json EvmJsonRpcHttpClient::get_transaction_by_hash(
        const std::string &tx_hash)
    {
        return call("eth_getTransactionByHash",
                    nlohmann::json::array({tx_hash}));
    }

    EvmJsonRpcWsClient::EvmJsonRpcWsClient(const std::string &rpc_ws_url)
        : runtime_(detail::EvmJsonRpcWsRuntime::create(rpc_ws_url))
    {
    }

    EvmJsonRpcWsClient::~EvmJsonRpcWsClient()
    {
        auto runtime = std::move(runtime_);
        if (runtime) runtime->shutdown();
    }

    void EvmJsonRpcWsClient::set_ping_interval_ms(int interval_ms)
    {
        auto runtime = runtime_;
        if (runtime) runtime->set_ping_interval_ms(interval_ms);
    }

    void EvmJsonRpcWsClient::set_auto_reconnect(bool enabled)
    {
        auto runtime = runtime_;
        if (runtime) runtime->set_auto_reconnect(enabled);
    }

    void EvmJsonRpcWsClient::on_log(EvmLogCallback callback)
    {
        auto runtime = runtime_;
        if (runtime) runtime->on_log(std::move(callback));
    }

    void EvmJsonRpcWsClient::on_pending_transaction(
        EvmPendingTxCallback callback)
    {
        auto runtime = runtime_;
        if (runtime) runtime->on_pending_transaction(std::move(callback));
    }

    void EvmJsonRpcWsClient::on_head(EvmJsonCallback callback)
    {
        auto runtime = runtime_;
        if (runtime) runtime->on_head(std::move(callback));
    }

    void EvmJsonRpcWsClient::on_error(EvmRpcErrorCallback callback)
    {
        auto runtime = runtime_;
        if (runtime) runtime->on_error(std::move(callback));
    }

    bool EvmJsonRpcWsClient::connect()
    {
        auto runtime = runtime_;
        return runtime && runtime->connect();
    }

    void EvmJsonRpcWsClient::disconnect()
    {
        auto runtime = runtime_;
        if (runtime) runtime->disconnect();
    }

    bool EvmJsonRpcWsClient::is_connected() const
    {
        auto runtime = runtime_;
        return runtime && runtime->is_connected();
    }

    void EvmJsonRpcWsClient::run()
    {
        auto runtime = runtime_;
        if (runtime) runtime->run();
    }

    void EvmJsonRpcWsClient::stop()
    {
        auto runtime = runtime_;
        if (runtime) runtime->stop();
    }

    bool EvmJsonRpcWsClient::subscribe_logs(const EvmLogFilter &filter)
    {
        auto runtime = runtime_;
        return runtime && runtime->subscribe_logs(filter);
    }

    bool EvmJsonRpcWsClient::subscribe_pending_transactions()
    {
        auto runtime = runtime_;
        return runtime && runtime->subscribe_pending_transactions();
    }

    bool EvmJsonRpcWsClient::subscribe_new_heads()
    {
        auto runtime = runtime_;
        return runtime && runtime->subscribe_new_heads();
    }
}
