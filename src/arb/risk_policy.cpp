#include "arb/risk_policy.hpp"
#include <utility>

namespace polymarket::arb
{

    RiskPolicy::RiskPolicy(ArbConfig config)
        : config_(std::move(config))
    {
    }

    GateDecision RiskPolicy::allow(const OpportunitySignal &signal)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (halted_)
        {
            return {false, last_reason_.empty() ? "trading_halted" : last_reason_};
        }
        if (signal.signal_detected_at_ns < cooldown_until_ns_)
        {
            return {false, last_reason_.empty() ? "cooldown_active" : last_reason_};
        }
        if (signal.snapshot_key == last_snapshot_key_)
        {
            return {false, "duplicate_snapshot"};
        }

        last_snapshot_key_ = signal.snapshot_key;
        return {true, ""};
    }

    void RiskPolicy::on_market_switched()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        last_snapshot_key_ = 0;
    }

    void RiskPolicy::on_execution_finished(const ExecutionReport &report)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const uint64_t reference_ns =
            report.response_at_ns > 0 ? report.response_at_ns : now_ns();

        if (report.one_filled &&
            (!report.flatten_report.has_value() || !report.flatten_report->success))
        {
            halted_ = true;
            last_reason_ = "flatten_failed_halted";
            cooldown_until_ns_ = reference_ns + (config_.cooldown_ms * 1000000ULL);
            return;
        }

        if (report.one_filled)
        {
            last_reason_ = "post_orphan_cooldown";
            cooldown_until_ns_ = reference_ns + (config_.cooldown_ms * 1000000ULL);
            return;
        }

        if (report.both_filled)
        {
            last_reason_ = "post_fill_cooldown";
            cooldown_until_ns_ = reference_ns + (config_.cooldown_ms * 1000000ULL);
            return;
        }

        if (report.both_rejected || report.dry_run || !report.error_msg.empty())
        {
            last_reason_ = report.dry_run ? "dry_run_cooldown" : "post_reject_cooldown";
            cooldown_until_ns_ = reference_ns + (config_.cooldown_ms * 1000000ULL);
            return;
        }

        last_reason_.clear();
    }

    bool RiskPolicy::halted() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return halted_;
    }

    std::string RiskPolicy::current_block_reason(uint64_t now_ns) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (halted_)
        {
            return last_reason_.empty() ? "trading_halted" : last_reason_;
        }
        if (now_ns < cooldown_until_ns_)
        {
            return last_reason_.empty() ? "cooldown_active" : last_reason_;
        }
        return "";
    }

} // namespace polymarket::arb
