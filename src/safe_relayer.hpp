#pragma once

#include "http_client.hpp"
#include "polymarket_contracts.hpp"
#include "position_client.hpp"
#include <array>
#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <vector>

// Gnosis Safe transactions submitted through the Polymarket relayer.
// Mirrors py-sdk _internal/actions/relayer/{gasless,submit,poll,signing/safe}.py.
namespace polymarket
{
    class OrderSigner;
}

namespace polymarket::detail
{
    constexpr unsigned kSafeOperationCall = 0;
    constexpr unsigned kSafeOperationDelegateCall = 1;

    // CREATE2 address of the Polymarket Safe owned by `owner`.
    std::string derive_safe_address(const std::string &owner, const PolymarketContracts &contracts);

    // MultiSend(bytes) call batching `calls` as plain CALLs.
    ContractCall encode_safe_multisend(const std::vector<ContractCall> &calls, const std::string &multisend);

    // The single call a Safe executes for `calls`: the call itself, or a
    // DELEGATECALL into MultiSend for several.
    struct SafeCall
    {
        ContractCall call;
        unsigned operation{kSafeOperationCall};
    };
    SafeCall resolve_safe_call(const std::vector<ContractCall> &calls, const PolymarketContracts &contracts);

    // EIP-712 SafeTx digest with zero gas/refund fields (what the relayer submits).
    std::array<uint8_t, 32> safe_transaction_digest(const std::string &safe, uint64_t chain_id,
                                                    const SafeCall &safe_call, const std::string &nonce);

    // eth_sign-style Safe signature: sign keccak("\x19Ethereum Signed Message:\n32" || digest)
    // and add 4 to v, which tells the Safe how to verify it.
    std::string sign_safe_digest(OrderSigner &signer, const std::array<uint8_t, 32> &digest);

    nlohmann::json build_safe_payload(const std::string &signer_address, const std::string &safe,
                                      const SafeCall &safe_call, const std::string &nonce,
                                      const std::string &signature, const std::string &metadata);

    // Non-2xx relayer response.
    class RelayerRequestError : public std::runtime_error
    {
    public:
        RelayerRequestError(const std::string &endpoint, long status, std::string body);
        long status() const { return status_; }
        const std::string &body() const { return body_; }

    private:
        long status_;
        std::string body_;
    };

    // Transient submit rejections worth retrying with a fresh nonce: rate
    // limits, a busy wallet, or a stale (lower than on-chain) nonce.
    bool is_retryable_submit_error(const RelayerRequestError &error);

    struct RelayerTransaction
    {
        std::string transaction_id;
        std::string transaction_hash; // empty until the relayer has one
        std::string state;            // STATE_NEW ... STATE_CONFIRMED / STATE_FAILED / STATE_INVALID
        std::string error_message;
    };

    class RelayerClient
    {
    public:
        RelayerClient(const std::string &base_url, std::string api_key, std::string api_key_address);

        // Next Safe nonce for the Safe owned by signer_address.
        std::string safe_nonce(const std::string &signer_address);
        RelayerTransaction submit(const nlohmann::json &payload);
        RelayerTransaction get_transaction(const std::string &transaction_id);

    private:
        nlohmann::json parse(const HttpResponse &response, const std::string &endpoint) const;

        HttpClient http_;
        std::map<std::string, std::string> headers_;
    };
} // namespace polymarket::detail
