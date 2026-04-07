#pragma once

#include "arb/arb_types.hpp"
#include "arb/execution_adapter.hpp"
#include "arb/execution_planner.hpp"
#include "arb/risk_policy.hpp"
#include "orderbook.hpp"

namespace polymarket::arb
{

    class ExecutionRunner
    {
    public:
        ExecutionRunner(ArbConfig config,
                        ExecutionAdapter &adapter,
                        RiskPolicy &risk_policy);

        ExecutionReport execute(const OpportunitySignal &signal,
                                const Orderbook &yes_book,
                                const Orderbook &no_book) const;

    private:
        FlattenRequest build_flatten_request(const ExecutionReport &report,
                                            const OpportunitySignal &signal,
                                            const Orderbook &yes_book,
                                            const Orderbook &no_book) const;

        ArbConfig config_;
        ExecutionPlanner planner_;
        ExecutionAdapter &adapter_;
        RiskPolicy &risk_policy_;
    };

} // namespace polymarket::arb
