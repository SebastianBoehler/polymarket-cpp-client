#include "arb/execution_runner.hpp"
#include <cassert>
#include <iostream>

using namespace polymarket;
using namespace polymarket::arb;

namespace
{
    class FakeAdapter : public ExecutionAdapter
    {
    public:
        bool live_enabled = true;
        SubmitResult submit_result;
        FlattenReport flatten_result;
        int flatten_calls = 0;

        bool live_trading_enabled() const override
        {
            return live_enabled;
        }

        SubmitResult submit(const ExecutionPlan &) override
        {
            return submit_result;
        }

        FlattenReport flatten(const FlattenRequest &request,
                              const PreparedMarket &) override
        {
            flatten_result.token_id = request.token_id;
            flatten_result.shares_requested = request.shares;
            ++flatten_calls;
            return flatten_result;
        }
    };

    OpportunitySignal make_signal()
    {
        OpportunitySignal signal;
        signal.prepared_market.market.condition_id = "condition-1";
        signal.prepared_market.market.token_yes = "yes-token";
        signal.prepared_market.market.token_no = "no-token";
        signal.prepared_market.metadata.tick_size = 0.01;
        signal.prepared_market.metadata.neg_risk = true;
        signal.adjusted_yes_price = 0.48;
        signal.adjusted_no_price = 0.49;
        signal.signal_detected_at_ns = now_ns();
        signal.snapshot_key = 7;
        return signal;
    }

    Orderbook make_book(const std::string &asset_id)
    {
        Orderbook book;
        book.asset_id = asset_id;
        book.timestamp_ns = now_ns();
        book.asks.push_back({0.49, 100.0});
        book.bids.push_back({0.47, 100.0});
        return book;
    }
} // namespace

int main()
{
    ArbConfig config;
    config.per_leg_size_usdc = 5.0;

    FakeAdapter filled_adapter;
    filled_adapter.submit_result.attempted = true;
    filled_adapter.submit_result.success = true;
    filled_adapter.submit_result.yes_leg = {.label = "YES", .token_id = "yes-token", .filled = true, .filled_shares = 10.0};
    filled_adapter.submit_result.no_leg = {.label = "NO", .token_id = "no-token", .filled = true, .filled_shares = 10.0};
    RiskPolicy filled_policy(config);
    ExecutionRunner filled_runner(config, filled_adapter, filled_policy);
    const auto both_filled = filled_runner.execute(make_signal(), make_book("yes-token"), make_book("no-token"));
    assert(both_filled.both_filled);
    assert(!both_filled.one_filled);
    assert(filled_adapter.flatten_calls == 0);

    FakeAdapter orphan_adapter;
    orphan_adapter.submit_result.attempted = true;
    orphan_adapter.submit_result.success = true;
    orphan_adapter.submit_result.response_at_ns = now_ns();
    orphan_adapter.submit_result.yes_leg = {.label = "YES", .token_id = "yes-token", .filled = true, .filled_shares = 10.0};
    orphan_adapter.submit_result.no_leg = {.label = "NO", .token_id = "no-token", .error_msg = "not_filled", .filled = false};
    orphan_adapter.flatten_result.attempted = true;
    orphan_adapter.flatten_result.success = false;
    orphan_adapter.flatten_result.error_msg = "fok_failed";
    RiskPolicy orphan_policy(config);
    ExecutionRunner orphan_runner(config, orphan_adapter, orphan_policy);
    const auto orphan = orphan_runner.execute(make_signal(), make_book("yes-token"), make_book("no-token"));
    assert(orphan.one_filled);
    assert(orphan.flatten_report.has_value());
    assert(!orphan.flatten_report->success);
    assert(orphan_adapter.flatten_calls == 1);
    assert(orphan_policy.halted());

    FakeAdapter rejected_adapter;
    rejected_adapter.submit_result.attempted = true;
    rejected_adapter.submit_result.success = true;
    rejected_adapter.submit_result.yes_leg = {.label = "YES", .token_id = "yes-token", .error_msg = "rejected", .filled = false};
    rejected_adapter.submit_result.no_leg = {.label = "NO", .token_id = "no-token", .error_msg = "rejected", .filled = false};
    RiskPolicy rejected_policy(config);
    ExecutionRunner rejected_runner(config, rejected_adapter, rejected_policy);
    const auto both_rejected = rejected_runner.execute(make_signal(), make_book("yes-token"), make_book("no-token"));
    assert(both_rejected.both_rejected);
    assert(!both_rejected.both_filled);
    assert(!both_rejected.one_filled);

    std::cout << "arb_execution_runner_test passed\n";
    return 0;
}
