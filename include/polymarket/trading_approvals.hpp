#pragma once

#include "polymarket/polymarket_contracts.hpp"
#include <string>
#include <vector>

namespace polymarket
{
    // 2^256 - 1, the allowance the trading approval set grants.
    inline constexpr const char *max_uint256 =
        "115792089237316195423570985008687907853269984665640564039457584007913129639935";

    // An ERC-20 allowance of `amount` (base-10 base units) from the wallet to `spender`.
    struct Erc20TradingApproval
    {
        std::string token_address;
        std::string spender;
        std::string amount;
    };

    // ERC-1155 setApprovalForAll(operator_address, true) by the wallet on `token_address`.
    struct Erc1155TradingApproval
    {
        std::string token_address;
        std::string operator_address;
    };

    // A set of approvals: ERC-20 allowances first, then ERC-1155 operators,
    // in the order the official SDKs submit them.
    struct TradingApprovals
    {
        std::vector<Erc20TradingApproval> erc20;
        std::vector<Erc1155TradingApproval> erc1155;

        bool empty() const { return erc20.empty() && erc1155.empty(); }
    };

    struct TradingApprovalsState
    {
        TradingApprovals missing; // only the approvals the wallet still lacks
        bool is_fully_approved{false};
    };

    // Every approval a wallet needs before it can trade, split, merge and
    // redeem: pUSD allowances (max) for the exchanges, collateral adapters, V2
    // router, exchange V3 and perps deposit contract, and ERC-1155 operator
    // approval on ConditionalTokens and the V2 position manager. Matches
    // py-sdk and ts-sdk setup_trading_approvals.
    TradingApprovals required_trading_approvals(const PolymarketContracts &contracts);

} // namespace polymarket
