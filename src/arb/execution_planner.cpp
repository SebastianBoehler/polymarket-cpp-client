#include "arb/execution_planner.hpp"
#include "arb/price_utils.hpp"
#include <algorithm>
#include <utility>

namespace polymarket::arb
{

    ExecutionPlanner::ExecutionPlanner(ArbConfig config)
        : config_(std::move(config))
    {
    }

    std::optional<ExecutionPlan> ExecutionPlanner::create_plan(const OpportunitySignal &signal) const
    {
        const double yes_shares =
            round_shares_for_polymarket(config_.per_leg_size_usdc / signal.adjusted_yes_price);
        const double no_shares =
            round_shares_for_polymarket(config_.per_leg_size_usdc / signal.adjusted_no_price);
        if (yes_shares <= 0.0 || no_shares <= 0.0)
        {
            return std::nullopt;
        }

        const double yes_spend =
            round_down_currency(signal.adjusted_yes_price * yes_shares);
        const double no_spend =
            round_down_currency(signal.adjusted_no_price * no_shares);
        const double guaranteed_payout = std::min(yes_shares, no_shares);
        const double total_spend = yes_spend + no_spend;
        const double expected_profit = guaranteed_payout - total_spend;
        if (expected_profit <= 0.0)
        {
            return std::nullopt;
        }

        ExecutionPlan plan;
        plan.signal = signal;
        plan.plan_ready_at_ns = now_ns();
        plan.total_spend_usdc = total_spend;
        plan.guaranteed_payout_usdc = guaranteed_payout;
        plan.expected_profit_usdc = expected_profit;
        plan.worst_case_orphan_exposure_usdc = std::max(yes_spend, no_spend);

        plan.yes_leg.label = "YES";
        plan.yes_leg.token_id = signal.prepared_market.market.token_yes;
        plan.yes_leg.limit_price = signal.adjusted_yes_price;
        plan.yes_leg.size_shares = yes_shares;
        plan.yes_leg.spend_usdc = yes_spend;
        plan.yes_leg.order_params.token_id = signal.prepared_market.market.token_yes;
        plan.yes_leg.order_params.price = signal.adjusted_yes_price;
        plan.yes_leg.order_params.size = yes_shares;
        plan.yes_leg.order_params.side = OrderSide::BUY;
        plan.yes_leg.order_params.neg_risk = signal.prepared_market.metadata.neg_risk;

        plan.no_leg.label = "NO";
        plan.no_leg.token_id = signal.prepared_market.market.token_no;
        plan.no_leg.limit_price = signal.adjusted_no_price;
        plan.no_leg.size_shares = no_shares;
        plan.no_leg.spend_usdc = no_spend;
        plan.no_leg.order_params.token_id = signal.prepared_market.market.token_no;
        plan.no_leg.order_params.price = signal.adjusted_no_price;
        plan.no_leg.order_params.size = no_shares;
        plan.no_leg.order_params.side = OrderSide::BUY;
        plan.no_leg.order_params.neg_risk = signal.prepared_market.metadata.neg_risk;

        return plan;
    }

} // namespace polymarket::arb
