#pragma once

#include <cstdint>
#include <string>

namespace polymarket
{
    // On-chain contracts used by Polymarket. polygon_mainnet() matches the
    // production environment of the official py-sdk and ts-sdk; every field can
    // be overridden for forks or local test chains.
    struct PolymarketContracts
    {
        uint64_t chain_id{137};

        // Collateral and outcome tokens
        std::string collateral_token;   // pUSD, 6 decimals
        std::string conditional_tokens; // Gnosis ConditionalTokens (ERC-1155 positions)
        std::string neg_risk_adapter;   // ERC-1155 holder for neg-risk positions

        // Split/merge/redeem targets for CTF markets; they take the
        // ConditionalTokens ABI with pUSD as collateral.
        std::string collateral_adapter;
        std::string neg_risk_collateral_adapter;

        // CLOB exchanges
        std::string standard_exchange;
        std::string neg_risk_exchange;
        std::string exchange_v3;

        // Protocol V2 markets
        std::string protocol_v2_router;
        std::string position_manager; // ERC-1155 positions for V2 markets
        std::string binary_module;
        std::string neg_risk_module;
        std::string combinatorial_module;
        std::string auto_redeem_operator;

        // Perps collateral deposits; a pUSD spender in the trading approval set
        std::string perps_deposit_contract;

        // Wallet infrastructure
        std::string proxy_factory;
        std::string proxy_implementation;
        std::string safe_factory;
        std::string safe_init_code_hash; // bytes32, not an address
        std::string safe_multisend;
        std::string relay_hub;
        std::string deposit_wallet_factory;
        std::string deposit_wallet_implementation;
        std::string deposit_wallet_beacon;

        static PolymarketContracts polygon_mainnet();
        // Preset for chain_id; throws std::invalid_argument for chains without one.
        static PolymarketContracts for_chain(uint64_t chain_id);

        // Throws std::invalid_argument naming the first malformed field.
        void validate() const;

        const std::string &ctf_collateral_adapter(bool neg_risk) const
        {
            return neg_risk ? neg_risk_collateral_adapter : collateral_adapter;
        }
        const std::string &exchange(bool neg_risk) const
        {
            return neg_risk ? neg_risk_exchange : standard_exchange;
        }
    };

} // namespace polymarket
