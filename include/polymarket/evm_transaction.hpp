#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace polymarket
{
    class OrderSigner;

    // Pre-EIP-1559 transaction, signed with EIP-155 replay protection.
    // Integer fields take base-10 strings or 0x-hex quantities (as returned by
    // JSON-RPC), so values from eth_gasPrice etc. can be passed through as-is.
    struct EvmLegacyTransaction
    {
        std::string nonce;
        std::string gas_price; // wei
        std::string gas_limit;
        std::string to;        // 20-byte address; contract creation is not supported
        std::string value = "0";
        std::string data = "0x";
        uint64_t chain_id{137};
    };

    struct EvmSignedTransaction
    {
        std::string raw_transaction;  // 0x-hex, ready for eth_sendRawTransaction
        std::string transaction_hash; // 0x-hex keccak256 of raw_transaction
    };

    // keccak256 of RLP([nonce, gasPrice, gas, to, value, data, chainId, 0, 0]).
    // Throws std::invalid_argument on malformed fields.
    std::array<uint8_t, 32> evm_legacy_transaction_signing_hash(const EvmLegacyTransaction &tx);

    EvmSignedTransaction evm_sign_legacy_transaction(OrderSigner &signer,
                                                     const EvmLegacyTransaction &tx);

} // namespace polymarket
