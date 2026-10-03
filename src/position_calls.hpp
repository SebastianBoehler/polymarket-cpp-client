#pragma once

#include "polymarket_contracts.hpp"
#include "position_client.hpp"
#include <array>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

// Calldata builders and response parsing for PositionClient. Mirrors
// py-sdk _internal/actions/relayer/{calls,positions}.py.
namespace polymarket::detail
{
    // Requires 0x + 32 bytes of hex; returns it lowercased.
    std::string normalize_condition_id(const std::string &condition_id);

    // Protocol V2 condition ids are bytes31. A bytes32 id whose last byte is an
    // outcome index (00 or 01) is truncated, as in the official SDKs.
    std::string v2_condition_id_bytes31(const std::string &condition_id);

    ContractCall ctf_split_position_call(const std::string &adapter, const std::string &collateral,
                                         const std::string &condition_id, const std::string &amount);
    ContractCall ctf_merge_positions_call(const std::string &adapter, const std::string &collateral,
                                          const std::string &condition_id, const std::string &amount);
    ContractCall ctf_redeem_positions_call(const std::string &adapter, const std::string &collateral,
                                           const std::string &condition_id);
    ContractCall router_split_call(const std::string &router, const std::string &condition_id,
                                   const std::string &amount);
    ContractCall router_merge_call(const std::string &router, const std::string &condition_id,
                                   const std::string &amount);
    ContractCall router_redeem_call(const std::string &router, const std::string &condition_id,
                                    unsigned outcome_index, const std::string &amount);

    ContractCall split_call(const MarketPositionContext &market, const PolymarketContracts &contracts,
                            const std::string &amount);
    ContractCall merge_call(const MarketPositionContext &market, const PolymarketContracts &contracts,
                            const std::string &amount);

    // balanceOfBatch([owner, owner], token_ids) on the market's ERC-1155 contract.
    ContractCall balance_of_batch_call(const MarketPositionContext &market, const std::string &owner);
    std::array<std::string, 2> decode_binary_balances(const std::string &return_data);

    // Builds the context from a Gamma /markets response body (a JSON array).
    MarketPositionContext market_context_from_gamma(const nlohmann::json &markets,
                                                    const std::string &condition_id,
                                                    const PolymarketContracts &contracts);

    // "max" -> min(balances); otherwise a positive amount no larger than that.
    std::string resolve_merge_amount(const std::string &condition_id,
                                     const std::array<std::string, 2> &balances,
                                     const std::string &requested);

    // Throws std::invalid_argument unless amount is a positive uint256.
    void require_positive_amount(const std::string &amount, const char *label);

    bool uint256_is_zero(const std::string &value);
} // namespace polymarket::detail
