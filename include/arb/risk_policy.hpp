#pragma once

#include "arb/arb_types.hpp"
#include <mutex>
#include <string>

namespace polymarket::arb
{

    class RiskPolicy
    {
    public:
        explicit RiskPolicy(ArbConfig config);

        GateDecision allow(const OpportunitySignal &signal);
        void on_market_switched();
        void on_execution_finished(const ExecutionReport &report);
        bool halted() const;
        std::string current_block_reason(uint64_t now_ns) const;

    private:
        ArbConfig config_;
        mutable std::mutex mutex_;
        bool halted_{false};
        uint64_t cooldown_until_ns_{0};
        uint64_t last_snapshot_key_{0};
        std::string last_reason_;
    };

} // namespace polymarket::arb
