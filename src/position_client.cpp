#include "polymarket/position_client.hpp"
#include "position_calls.hpp"
#include "position_client_impl.hpp"
#include <set>

namespace polymarket
{
    PositionClient::PositionClient(PositionClientConfig config)
    {
        config.contracts.validate();
        if (config.rpc_url.empty())
            throw std::invalid_argument("PositionClientConfig.rpc_url is required");
        impl_ = std::make_unique<Impl>(config);
    }

    PositionClient::~PositionClient() = default;

    const std::string &PositionClient::wallet_address() const
    {
        return impl_->wallet;
    }

    MarketPositionContext PositionClient::resolve_market(const std::string &condition_id, bool closed_only)
    {
        const auto normalized = detail::normalize_requested_condition_id(condition_id);
        std::string path = "/markets?condition_ids=" + normalized;
        if (closed_only)
            path += "&closed=true";
        const auto response = impl_->gamma.get(path);
        if (!response.ok())
            throw std::runtime_error("Gamma market lookup failed for condition " + normalized + ": status " +
                                     std::to_string(response.status_code) + " " + response.error);
        nlohmann::json body;
        try
        {
            body = nlohmann::json::parse(response.body);
        }
        catch (const nlohmann::json::parse_error &)
        {
            throw std::runtime_error("Gamma market lookup returned invalid JSON for condition " + normalized);
        }
        if (body.is_array() && body.empty())
            throw std::invalid_argument("no " + std::string(closed_only ? "closed" : "open") +
                                        " market found for condition " + normalized +
                                        (closed_only ? "" : " (closed markets can only be redeemed)"));
        return detail::market_context_from_gamma(body, normalized, impl_->contracts);
    }

    std::array<std::string, 2> PositionClient::position_balances(const MarketPositionContext &market)
    {
        const auto call = detail::balance_of_batch_call(market, wallet_address());
        return detail::decode_binary_balances(impl_->rpc->eth_call({call.to, call.data, "", ""}));
    }

    TransactionHandle PositionClient::execute_calls(const std::vector<ContractCall> &calls,
                                                    const std::string &metadata)
    {
        if (calls.empty())
            throw std::invalid_argument("execute_calls needs at least one call");
        if (metadata.size() > 500)
            throw std::invalid_argument("metadata must be at most 500 characters");
        if (impl_->wallet_type == SignatureType::POLY_GNOSIS_SAFE)
            return impl_->execute_from_safe(calls, metadata);
        return impl_->execute_from_eoa(calls);
    }

    TransactionHandle PositionClient::split_position(const std::string &condition_id, const std::string &amount)
    {
        detail::require_positive_amount(amount, "split amount");
        const auto market = resolve_market(condition_id);
        return execute_calls({detail::split_call(market, impl_->contracts, amount)},
                             "Split " + amount + " positions for condition " + market.condition_id);
    }

    TransactionHandle PositionClient::merge_positions(const std::string &condition_id, const std::string &amount)
    {
        const auto market = resolve_market(condition_id);
        const auto resolved = detail::resolve_merge_amount(market.condition_id, position_balances(market), amount);
        return execute_calls({detail::merge_call(market, impl_->contracts, resolved)},
                             "Merge " + resolved + " positions for condition " + market.condition_id);
    }

    TransactionHandle PositionClient::merge_multiple_positions(const std::vector<MergePositionRequest> &requests)
    {
        if (requests.empty())
            throw std::invalid_argument("merge_multiple_positions needs at least one request");
        std::set<std::string> seen;
        for (const auto &request : requests)
        {
            if (!seen.insert(detail::normalize_requested_condition_id(request.condition_id)).second)
                throw std::invalid_argument("merge requests must reference distinct conditions");
        }
        // bytes31 and bytes32 forms of one V2 condition only meet after Gamma resolves them.
        std::set<std::string> resolved_conditions;
        std::vector<ContractCall> calls;
        for (const auto &request : requests)
        {
            const auto market = resolve_market(request.condition_id);
            if (!resolved_conditions.insert(market.condition_id).second)
                throw std::invalid_argument("merge requests must reference distinct conditions");
            const auto resolved =
                detail::resolve_merge_amount(market.condition_id, position_balances(market), request.amount);
            calls.push_back(detail::merge_call(market, impl_->contracts, resolved));
        }
        return execute_calls(calls, "Merge " + std::to_string(calls.size()) + " positions");
    }

    TransactionHandle PositionClient::redeem_positions(const std::string &condition_id)
    {
        const auto market = resolve_market(condition_id, true);
        const auto balances = position_balances(market);
        if (detail::uint256_is_zero(balances[0]) && detail::uint256_is_zero(balances[1]))
            throw std::invalid_argument("no position balance to redeem for condition " + market.condition_id);

        if (market.protocol == MarketProtocol::Ctf)
            return execute_calls({detail::ctf_redeem_positions_call(market.operator_contract,
                                                                    impl_->contracts.collateral_token,
                                                                    market.condition_id)},
                                 "Redeem positions for condition " + market.condition_id);

        std::vector<ContractCall> calls;
        for (unsigned outcome = 0; outcome < 2; ++outcome)
        {
            if (!detail::uint256_is_zero(balances[outcome]))
                calls.push_back(detail::router_redeem_call(market.operator_contract, market.condition_id, outcome,
                                                           balances[outcome]));
        }
        return execute_calls(calls, "Redeem positions for condition " + market.condition_id);
    }

} // namespace polymarket
