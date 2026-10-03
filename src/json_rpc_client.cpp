#include "json_rpc_client.hpp"
#include "json_rpc_ws_runtime.hpp"

#include <cctype>
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

        [[noreturn]] void invalid_result(const std::string &method, const std::string &reason)
        {
            throw std::runtime_error("invalid JSON-RPC result: " + method + " " + reason);
        }

        bool is_hex_digits(const std::string &text, size_t from)
        {
            for (size_t i = from; i < text.size(); ++i)
            {
                if (!std::isxdigit(static_cast<unsigned char>(text[i])))
                    return false;
            }
            return true;
        }

        bool is_quantity(const nlohmann::json &value)
        {
            if (!value.is_string())
                return false;
            const auto &text = value.get_ref<const std::string &>();
            return text.size() > 2 && text.size() <= 66 && text.rfind("0x", 0) == 0 &&
                   is_hex_digits(text, 2);
        }

        bool is_hex_data(const nlohmann::json &value)
        {
            if (!value.is_string())
                return false;
            const auto &text = value.get_ref<const std::string &>();
            return text.rfind("0x", 0) == 0 && text.size() % 2 == 0 && is_hex_digits(text, 2);
        }

        bool is_hash(const nlohmann::json &value)
        {
            return is_hex_data(value) && value.get_ref<const std::string &>().size() == 66;
        }

        std::string expect_quantity(const nlohmann::json &value, const std::string &method)
        {
            if (!is_quantity(value))
                invalid_result(method, "must be a 0x-hex quantity");
            return value.get<std::string>();
        }

        std::string receipt_field(const nlohmann::json &receipt, const char *key,
                                  bool (*valid)(const nlohmann::json &))
        {
            if (!receipt.contains(key) || !valid(receipt.at(key)))
                invalid_result("eth_getTransactionReceipt", std::string("field ") + key + " is missing or malformed");
            return receipt.at(key).get<std::string>();
        }
    }

    nlohmann::json EvmCallRequest::to_json() const
    {
        nlohmann::json out = {{"to", to}, {"data", data}};
        if (!from.empty())
            out["from"] = from;
        if (!value.empty())
            out["value"] = value;
        return out;
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

    uint64_t EvmJsonRpcHttpClient::chain_id()
    {
        const auto quantity = expect_quantity(call("eth_chainId", nlohmann::json::array()), "eth_chainId");
        const auto digits = quantity.substr(2);
        const auto first = digits.find_first_not_of('0');
        if (first != std::string::npos && digits.size() - first > 16)
            invalid_result("eth_chainId", "exceeds 64 bits");
        return std::stoull(digits, nullptr, 16);
    }

    std::string EvmJsonRpcHttpClient::get_transaction_count(const std::string &address,
                                                            const std::string &block_tag)
    {
        return expect_quantity(
            call("eth_getTransactionCount", nlohmann::json::array({address, block_tag})),
            "eth_getTransactionCount");
    }

    std::string EvmJsonRpcHttpClient::gas_price()
    {
        return expect_quantity(call("eth_gasPrice", nlohmann::json::array()), "eth_gasPrice");
    }

    std::string EvmJsonRpcHttpClient::estimate_gas(const EvmCallRequest &request)
    {
        return expect_quantity(call("eth_estimateGas", nlohmann::json::array({request.to_json()})),
                               "eth_estimateGas");
    }

    std::string EvmJsonRpcHttpClient::eth_call(const EvmCallRequest &request,
                                               const std::string &block_tag)
    {
        auto result = call("eth_call", nlohmann::json::array({request.to_json(), block_tag}));
        if (!is_hex_data(result))
            invalid_result("eth_call", "must be 0x-hex data");
        return result.get<std::string>();
    }

    std::string EvmJsonRpcHttpClient::send_raw_transaction(const std::string &raw_transaction)
    {
        auto result = call("eth_sendRawTransaction", nlohmann::json::array({raw_transaction}));
        if (!is_hash(result))
            invalid_result("eth_sendRawTransaction", "must be a 32-byte transaction hash");
        return result.get<std::string>();
    }

    std::optional<EvmTransactionReceipt> EvmJsonRpcHttpClient::get_transaction_receipt(
        const std::string &tx_hash)
    {
        auto result = call("eth_getTransactionReceipt", nlohmann::json::array({tx_hash}));
        if (result.is_null())
            return std::nullopt;
        if (!result.is_object())
            invalid_result("eth_getTransactionReceipt", "must be an object or null");

        EvmTransactionReceipt receipt;
        receipt.transaction_hash = receipt_field(result, "transactionHash", is_hash);
        receipt.block_hash = receipt_field(result, "blockHash", is_hash);
        receipt.block_number = receipt_field(result, "blockNumber", is_quantity);
        receipt.gas_used = receipt_field(result, "gasUsed", is_quantity);
        if (result.contains("effectiveGasPrice"))
            receipt.effective_gas_price = receipt_field(result, "effectiveGasPrice", is_quantity);

        const auto status = receipt_field(result, "status", is_quantity);
        if (status != "0x1" && status != "0x0")
            invalid_result("eth_getTransactionReceipt", "status must be 0x0 or 0x1");
        receipt.success = status == "0x1";

        if (!result.contains("logs") || !result.at("logs").is_array())
            invalid_result("eth_getTransactionReceipt", "logs must be an array");
        for (const auto &raw : result.at("logs"))
            receipt.logs.push_back(evm_log_from_json(raw));
        return receipt;
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
