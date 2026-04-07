#include "arb/execution_planner.hpp"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace polymarket::arb;

namespace
{
    OpportunitySignal make_signal(double yes_price, double no_price)
    {
        OpportunitySignal signal;
        signal.prepared_market.market.token_yes = "yes-token";
        signal.prepared_market.market.token_no = "no-token";
        signal.prepared_market.metadata.neg_risk = true;
        signal.adjusted_yes_price = yes_price;
        signal.adjusted_no_price = no_price;
        signal.effective_combined = yes_price + no_price;
        signal.signal_detected_at_ns = 1;
        signal.snapshot_key = 42;
        return signal;
    }
} // namespace

int main()
{
    ArbConfig config;
    config.per_leg_size_usdc = 5.0;
    config.trigger_threshold = 0.99;

    ExecutionPlanner planner(config);
    const auto profitable = planner.create_plan(make_signal(0.48, 0.49));
    assert(profitable.has_value());
    assert(std::fabs(profitable->yes_leg.size_shares - 10.4166) < 0.0001);
    assert(std::fabs(profitable->no_leg.size_shares - 10.204) < 0.0001);
    assert(profitable->expected_profit_usdc > 0.0);
    assert(profitable->yes_leg.order_params.neg_risk.value());

    const auto unprofitable = planner.create_plan(make_signal(0.51, 0.50));
    assert(!unprofitable.has_value());

    std::cout << "arb_execution_planner_test passed\n";
    return 0;
}
