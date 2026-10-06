#include "approval_calls.hpp"
#include "polymarket/position_client.hpp"
#include "position_calls.hpp"
#include "position_client_impl.hpp"

namespace polymarket
{
    TradingApprovalsState
    PositionClient::get_trading_approvals_state(const std::optional<std::string> &wallet)
    {
        const auto owner = wallet.value_or(wallet_address());
        detail::require_address(owner, "wallet");
        const auto required = required_trading_approvals(impl_->contracts);
        std::vector<std::string> results;
        for (const auto &call : detail::trading_approval_check_calls(required, owner))
            results.push_back(impl_->rpc->eth_call({call.to, call.data, "", ""}));
        return detail::trading_approvals_state_from_results(required, results);
    }

    std::optional<TransactionOutcome>
    PositionClient::setup_trading_approvals(std::chrono::milliseconds timeout)
    {
        // Read over RPC rather than from indexed data, which can lag recent grants.
        const auto state = get_trading_approvals_state();
        if (state.is_fully_approved) return std::nullopt;
        const auto handle =
            execute_calls(detail::trading_approval_calls(state.missing), "Trading setup approvals");
        try
        {
            return handle.wait(timeout);
        }
        catch (const std::exception &error)
        {
            // An EOA batch already mined its earlier approvals; keep their hashes.
            const auto &hashes = handle.transaction_hashes();
            if (hashes.size() < 2)
                throw;
            throw PartialBatchError(hashes, hashes.size() - 1, hashes.size(),
                                    std::current_exception(), error.what());
        }
    }

    TransactionHandle PositionClient::approve_erc20(const std::string &token_address,
                                                    const std::string &spender_address,
                                                    const std::string &amount,
                                                    const std::string &metadata)
    {
        detail::require_address(token_address, "token_address");
        detail::require_address(spender_address, "spender_address");
        const auto call = detail::erc20_approve_call(token_address, spender_address,
                                                     detail::resolve_approval_amount(amount));
        return execute_calls({call}, metadata.empty() ? "Approve " + amount + " of " +
                                                            token_address + " to " + spender_address
                                                      : metadata);
    }

    TransactionHandle PositionClient::approve_erc1155_for_all(const std::string &token_address,
                                                              const std::string &operator_address,
                                                              bool approved,
                                                              const std::string &metadata)
    {
        detail::require_address(token_address, "token_address");
        detail::require_address(operator_address, "operator_address");
        const auto call =
            detail::erc1155_set_approval_for_all_call(token_address, operator_address, approved);
        return execute_calls({call}, metadata.empty()
                                         ? std::string(approved ? "Approve " : "Revoke ") +
                                               operator_address + " on " + token_address
                                         : metadata);
    }

    TransactionHandle PositionClient::transfer_erc20(const std::string &token_address,
                                                     const std::string &recipient_address,
                                                     const std::string &amount,
                                                     const std::string &metadata)
    {
        detail::require_address(token_address, "token_address");
        detail::require_address(recipient_address, "recipient_address");
        detail::require_positive_amount(amount, "transfer amount");
        const auto call = detail::erc20_transfer_call(token_address, recipient_address, amount);
        return execute_calls({call}, metadata.empty()
                                         ? "Transfer " + amount + " of " + token_address + " to " +
                                               recipient_address
                                         : metadata);
    }
} // namespace polymarket
