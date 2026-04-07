#include "arb/execution_runner.hpp"
#include "arb/price_utils.hpp"

namespace polymarket::arb
{

    namespace
    {
        std::string build_signal_id(const OpportunitySignal &signal)
        {
            return signal.prepared_market.market.condition_id + ":" +
                   std::to_string(signal.snapshot_key);
        }
    } // namespace

    ExecutionRunner::ExecutionRunner(ArbConfig config,
                                     ExecutionAdapter &adapter,
                                     RiskPolicy &risk_policy)
        : config_(std::move(config)),
          planner_(config_),
          adapter_(adapter),
          risk_policy_(risk_policy)
    {
    }

    ExecutionReport ExecutionRunner::execute(const OpportunitySignal &signal,
                                             const Orderbook &yes_book,
                                             const Orderbook &no_book) const
    {
        ExecutionReport report;
        report.dry_run = !adapter_.live_trading_enabled();
        report.signal_id = build_signal_id(signal);
        report.signal_detected_at_ns = signal.signal_detected_at_ns;

        const auto plan = planner_.create_plan(signal);
        if (!plan.has_value())
        {
            report.error_msg = "plan_rejected";
            report.cooldown_reason = "plan_rejected";
            return report;
        }

        report.plan_created = true;
        report.plan_ready_at_ns = plan->plan_ready_at_ns;

        const auto submit = adapter_.submit(*plan);
        report.submit_attempted = submit.attempted;
        report.submit_succeeded = submit.success;
        report.signed_at_ns = submit.signed_at_ns;
        report.submitted_at_ns = submit.submitted_at_ns;
        report.response_at_ns = submit.response_at_ns;
        report.error_msg = submit.error_msg;
        report.yes_leg = submit.yes_leg;
        report.no_leg = submit.no_leg;

        const int filled_legs =
            static_cast<int>(report.yes_leg.filled) + static_cast<int>(report.no_leg.filled);
        report.both_filled = filled_legs == 2;
        report.one_filled = filled_legs == 1;
        report.both_rejected = filled_legs == 0 && !report.both_filled && !report.dry_run;
        report.orphan_fill_count = report.one_filled ? 1 : 0;

        if (report.one_filled)
        {
            const auto flatten =
                build_flatten_request(report, signal, yes_book, no_book);
            report.flatten_report =
                adapter_.flatten(flatten, signal.prepared_market);
        }

        risk_policy_.on_execution_finished(report);
        report.cooldown_reason = risk_policy_.current_block_reason(now_ns());
        return report;
    }

    FlattenRequest ExecutionRunner::build_flatten_request(const ExecutionReport &report,
                                                          const OpportunitySignal &signal,
                                                          const Orderbook &yes_book,
                                                          const Orderbook &no_book) const
    {
        const bool flatten_yes = report.yes_leg.filled;
        const Orderbook &flatten_book = flatten_yes ? yes_book : no_book;
        const LegReport &filled_leg = flatten_yes ? report.yes_leg : report.no_leg;

        const double tick_size = signal.prepared_market.metadata.tick_size;
        double limit_price = flatten_book.best_bid() - config_.slippage_buffer;
        limit_price = round_down_to_tick(limit_price, tick_size);
        if (limit_price <= 0.0)
        {
            limit_price = tick_size;
        }
        limit_price = clamp_price(limit_price);

        FlattenRequest request;
        request.token_id = filled_leg.token_id;
        request.shares = filled_leg.filled_shares > 0.0
                             ? filled_leg.filled_shares
                             : filled_leg.requested_shares;
        request.limit_price = limit_price;
        request.deadline_ns = now_ns() + (config_.orphan_unwind_timeout_ms * 1000000ULL);
        request.reason = "orphan_fill";
        return request;
    }

} // namespace polymarket::arb
