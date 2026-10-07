#include "safe_relayer.hpp"
#include "rate_limit_internal.hpp"
#include "polymarket/evm_abi.hpp"
#include "evm_uint.hpp"
#include "polymarket/evm_utils.hpp"
#include "polymarket/order_signer.hpp"
#include "position_calls.hpp"
#include <algorithm>
#include <cctype>
#include <regex>

namespace polymarket::detail
{
    namespace
    {
        const std::string kZeroAddress = "0x0000000000000000000000000000000000000000";

        std::array<uint8_t, 32> keccak_bytes(const std::vector<uint8_t> &data)
        {
            return keccak256(data);
        }

        void append(std::vector<uint8_t> &out, const std::vector<uint8_t> &bytes)
        {
            out.insert(out.end(), bytes.begin(), bytes.end());
        }

        void append(std::vector<uint8_t> &out, const std::array<uint8_t, 32> &bytes)
        {
            out.insert(out.end(), bytes.begin(), bytes.end());
        }

        std::string checksum_address(const std::vector<uint8_t> &address)
        {
            const auto lower = to_hex(address).substr(2);
            const auto hash = keccak256(lower);
            std::string out = "0x";
            for (size_t i = 0; i < lower.size(); ++i)
            {
                const int nibble = (hash[i / 2] >> (i % 2 == 0 ? 4 : 0)) & 0x0f;
                out += nibble >= 8 ? static_cast<char>(std::toupper(static_cast<unsigned char>(lower[i]))) : lower[i];
            }
            return out;
        }

        std::vector<uint8_t> address_bytes(const std::string &address, const char *label)
        {
            std::vector<uint8_t> bytes;
            try
            {
                bytes = from_hex(address);
            }
            catch (const std::invalid_argument &)
            {
            }
            if (bytes.size() != 20)
                throw std::invalid_argument(std::string(label) + " must be a 20-byte address");
            return bytes;
        }

        bool is_digits(const std::string &text)
        {
            if (text.empty())
                return false;
            for (const char c : text)
            {
                if (c < '0' || c > '9')
                    return false;
            }
            return true;
        }

        // a < b for base-10 digit strings.
        bool decimal_less(std::string a, std::string b)
        {
            a.erase(0, std::min(a.find_first_not_of('0'), a.size()));
            b.erase(0, std::min(b.find_first_not_of('0'), b.size()));
            return a.size() != b.size() ? a.size() < b.size() : a < b;
        }

        std::string string_field(const nlohmann::json &body, const char *key, const std::string &endpoint,
                                 bool required)
        {
            if (body.contains(key) && body.at(key).is_string())
                return body.at(key).get<std::string>();
            if (!required && (!body.contains(key) || body.at(key).is_null()))
                return {};
            throw std::runtime_error("invalid relayer response from " + endpoint + ": " + key +
                                     " is missing or not a string");
        }
    } // namespace

    std::string derive_safe_address(const std::string &owner, const PolymarketContracts &contracts)
    {
        const auto salt = keccak_bytes(evm_abi_encode({EvmAbiValue::address(owner)}));
        std::vector<uint8_t> preimage{0xff};
        append(preimage, address_bytes(contracts.safe_factory, "safe_factory"));
        append(preimage, salt);
        append(preimage, from_hex(contracts.safe_init_code_hash));
        const auto hash = keccak_bytes(preimage);
        return checksum_address(std::vector<uint8_t>(hash.begin() + 12, hash.end()));
    }

    ContractCall encode_safe_multisend(const std::vector<ContractCall> &calls, const std::string &multisend)
    {
        if (calls.empty())
            throw std::invalid_argument("MultiSend needs at least one call");
        std::vector<uint8_t> packed;
        for (const auto &call : calls)
        {
            const auto data = from_hex(call.data);
            packed.push_back(static_cast<uint8_t>(kSafeOperationCall));
            append(packed, address_bytes(call.to, "call target"));
            append(packed, parse_uint256_word(call.value));
            append(packed, uint256_word(data.size()));
            append(packed, data);
        }
        return {multisend, evm_abi_encode_call("multiSend(bytes)", {EvmAbiValue::bytes(packed)})};
    }

    SafeCall resolve_safe_call(const std::vector<ContractCall> &calls, const PolymarketContracts &contracts)
    {
        if (calls.empty())
            throw std::invalid_argument("a Safe transaction needs at least one call");
        if (calls.size() == 1)
            return {calls.front(), kSafeOperationCall};
        return {encode_safe_multisend(calls, contracts.safe_multisend), kSafeOperationDelegateCall};
    }

    std::array<uint8_t, 32> safe_transaction_digest(const std::string &safe, uint64_t chain_id,
                                                    const SafeCall &safe_call, const std::string &nonce)
    {
        const auto domain_separator = keccak_bytes(evm_abi_encode(
            {EvmAbiValue::bytes32(to_hex(keccak256(std::string(
                 "EIP712Domain(uint256 chainId,address verifyingContract)")))),
             EvmAbiValue::uint256(chain_id), EvmAbiValue::address(safe)}));
        const auto struct_hash = keccak_bytes(evm_abi_encode(
            {EvmAbiValue::bytes32(to_hex(keccak256(std::string(
                 "SafeTx(address to,uint256 value,bytes data,uint8 operation,uint256 safeTxGas,"
                 "uint256 baseGas,uint256 gasPrice,address gasToken,address refundReceiver,uint256 nonce)")))),
             EvmAbiValue::address(safe_call.call.to), EvmAbiValue::uint256(safe_call.call.value),
             EvmAbiValue::bytes32(to_hex(keccak256(from_hex(safe_call.call.data)))),
             EvmAbiValue::unsigned_integer(safe_call.operation, 8), EvmAbiValue::uint256(0),
             EvmAbiValue::uint256(0), EvmAbiValue::uint256(0), EvmAbiValue::address(kZeroAddress),
             EvmAbiValue::address(kZeroAddress), EvmAbiValue::uint256(nonce)}));
        std::vector<uint8_t> message{0x19, 0x01};
        append(message, domain_separator);
        append(message, struct_hash);
        return keccak_bytes(message);
    }

    std::string sign_safe_digest(OrderSigner &signer, const std::array<uint8_t, 32> &digest)
    {
        const std::string prefix = "\x19" "Ethereum Signed Message:\n32";
        std::vector<uint8_t> message(prefix.begin(), prefix.end());
        append(message, digest);
        auto signature = from_hex(signer.sign_hash(keccak_bytes(message)));
        if (signature.size() != 65 || (signature[64] != 27 && signature[64] != 28))
            throw std::runtime_error("signer returned an unexpected signature layout");
        signature[64] = static_cast<uint8_t>(signature[64] + 4);
        return to_hex(signature);
    }

    nlohmann::json build_safe_payload(const std::string &signer_address, const std::string &safe,
                                      const SafeCall &safe_call, const std::string &nonce,
                                      const std::string &signature, const std::string &metadata)
    {
        nlohmann::json payload = {
            {"type", "SAFE"},
            {"from", signer_address},
            {"to", safe_call.call.to},
            {"proxyWallet", safe},
            {"data", safe_call.call.data},
            {"nonce", nonce},
            {"signature", signature},
            {"metadata", metadata},
            {"signatureParams",
             {{"baseGas", "0"},
              {"gasPrice", "0"},
              {"gasToken", kZeroAddress},
              {"operation", std::to_string(safe_call.operation)},
              {"refundReceiver", kZeroAddress},
              {"safeTxnGas", "0"}}}};
        if (!uint256_is_zero(safe_call.call.value))
            payload["value"] = safe_call.call.value;
        return payload;
    }

    RelayerRequestError::RelayerRequestError(const std::string &endpoint, long status, std::string body)
        : std::runtime_error("relayer " + endpoint + " failed with status " + std::to_string(status) + ": " +
                             body.substr(0, 500)),
          status_(status), body_(std::move(body))
    {
    }

    bool is_retryable_submit_error(const RelayerRequestError &error)
    {
        if (error.status() == 429)
            return true;
        if (error.status() != 400)
            return false;
        static const std::regex busy("wallet busy.*active action", std::regex::icase);
        static const std::regex in_flight("wallet has in-flight action", std::regex::icase);
        static const std::regex nonce_mismatch(R"(batch nonce\s+(\d+)\s+does not match on-chain nonce\s+(\d+))",
                                               std::regex::icase);
        const auto &body = error.body();
        if (std::regex_search(body, busy) || std::regex_search(body, in_flight))
            return true;
        std::smatch match;
        return std::regex_search(body, match, nonce_mismatch) && decimal_less(match[1].str(), match[2].str());
    }

    RelayerClient::RelayerClient(const std::string &base_url, std::string api_key,
                                 std::string api_key_address)
        : RelayerClient(base_url, std::move(api_key), std::move(api_key_address), RateLimitRetry{})
    {
    }

    RelayerClient::RelayerClient(const std::string &base_url, std::string api_key,
                                 std::string api_key_address,
                                 std::optional<RateLimitRetry> read_retry)
        : headers_{{"RELAYER_API_KEY", std::move(api_key)},
                   {"RELAYER_API_KEY_ADDRESS", std::move(api_key_address)}},
          read_retry_(read_retry)
    {
        http_.set_base_url(base_url);
    }

    nlohmann::json RelayerClient::parse(const HttpResponse &response, const std::string &endpoint) const
    {
        if (response.status_code < 200 || response.status_code >= 300)
            throw RelayerRequestError(endpoint, response.status_code,
                                      response.body.empty() ? response.error : response.body);
        try
        {
            auto body = nlohmann::json::parse(response.body);
            if (!body.is_object())
                throw std::runtime_error("invalid relayer response from " + endpoint + ": not an object");
            return body;
        }
        catch (const nlohmann::json::parse_error &)
        {
            throw std::runtime_error("invalid relayer response from " + endpoint + ": not JSON");
        }
    }

    std::string RelayerClient::safe_nonce(const std::string &signer_address)
    {
        const std::string endpoint = "/v1/account/transactions/params";
        const auto body =
            parse(retry_rate_limited(read_retry_,
                                     [&]
                                     {
                                         return http_.get(endpoint + "?address=" + signer_address +
                                                              "&type=SAFE",
                                                          headers_);
                                     }),
                  endpoint);
        const auto nonce = string_field(body, "nonce", endpoint, true);
        if (!is_digits(nonce))
            throw std::runtime_error("invalid relayer response from " + endpoint + ": nonce is not numeric");
        return nonce;
    }

    RelayerTransaction RelayerClient::submit(const nlohmann::json &payload)
    {
        const std::string endpoint = "/submit";
        const auto body = parse(http_.post(endpoint, payload.dump(), headers_), endpoint);
        RelayerTransaction tx;
        tx.transaction_id = string_field(body, "transactionID", endpoint, true);
        tx.transaction_hash = string_field(body, "transactionHash", endpoint, false);
        tx.state = string_field(body, "state", endpoint, true);
        if (tx.transaction_id.empty())
            throw std::runtime_error("invalid relayer response from /submit: empty transactionID");
        return tx;
    }

    RelayerTransaction RelayerClient::get_transaction(const std::string &transaction_id)
    {
        const std::string endpoint = "/v1/account/transactions/" + transaction_id;
        const auto body =
            parse(retry_rate_limited(read_retry_, [&] { return http_.get(endpoint, headers_); }),
                  endpoint);
        if (!body.contains("transaction_hash"))
            throw std::runtime_error("invalid relayer response from " + endpoint + ": missing transaction_hash");
        RelayerTransaction tx;
        tx.transaction_id = string_field(body, "transaction_id", endpoint, true);
        tx.transaction_hash = string_field(body, "transaction_hash", endpoint, false);
        tx.state = string_field(body, "state", endpoint, true);
        tx.error_message = string_field(body, "error_msg", endpoint, false);
        return tx;
    }
} // namespace polymarket::detail
