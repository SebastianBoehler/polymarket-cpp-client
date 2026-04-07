#pragma once

#include "arb/arb_types.hpp"
#include <optional>

namespace polymarket::arb
{

    class ExecutionPlanner
    {
    public:
        explicit ExecutionPlanner(ArbConfig config);

        std::optional<ExecutionPlan> create_plan(const OpportunitySignal &signal) const;

    private:
        ArbConfig config_;
    };

} // namespace polymarket::arb
