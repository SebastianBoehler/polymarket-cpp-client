#pragma once

#include "polymarket/position_client.hpp"
#include "polymarket/trading_approvals.hpp"
#include <string>
#include <vector>

// Calldata builders and result decoding for token approvals and transfers.
// Mirrors py-sdk _internal/actions/relayer/{calls,approvals}.py.
namespace polymarket::detail
{
    // Throws std::invalid_argument naming `label` unless value is 0x + 20 bytes of hex.
    void require_address(const std::string &value, const char *label);

    // "max" -> max_uint256; otherwise a uint256 (base-10 or 0x-hex) as given.
    std::string resolve_approval_amount(const std::string &amount);

    ContractCall erc20_approve_call(const std::string &token, const std::string &spender,
                                    const std::string &amount);
    ContractCall erc20_transfer_call(const std::string &token, const std::string &recipient,
                                     const std::string &amount);
    ContractCall erc1155_set_approval_for_all_call(const std::string &token,
                                                   const std::string &operator_address,
                                                   bool approved);

    // Read-only checks for eth_call.
    ContractCall erc20_allowance_call(const std::string &token, const std::string &owner,
                                      const std::string &spender);
    ContractCall erc1155_is_approved_for_all_call(const std::string &token,
                                                  const std::string &owner,
                                                  const std::string &operator_address);

    // Single-word return values. Throw std::runtime_error naming `context` on
    // anything but exactly one word (a bool must be 0 or 1).
    std::string decode_uint256_result(const std::string &return_data, const std::string &context);
    bool decode_bool_result(const std::string &return_data, const std::string &context);

    // One eth_call per approval, ERC-20 checks first, in `required` order.
    std::vector<ContractCall> trading_approval_check_calls(const TradingApprovals &required,
                                                           const std::string &owner);
    // Pairs results with trading_approval_check_calls. An ERC-20 approval is
    // missing when its allowance is below the required amount.
    TradingApprovalsState
    trading_approvals_state_from_results(const TradingApprovals &required,
                                         const std::vector<std::string> &results);

    // approve calls for the missing ERC-20 allowances, then setApprovalForAll(true) calls.
    std::vector<ContractCall> trading_approval_calls(const TradingApprovals &missing);
} // namespace polymarket::detail
