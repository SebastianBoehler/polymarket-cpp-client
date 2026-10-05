#include "polymarket/json_rpc_client.hpp"

#include <cctype>
#include <stdexcept>

// Transaction-related JSON-RPC calls; results are validated before use.
namespace polymarket
{
    namespace
    {
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

} // namespace polymarket
