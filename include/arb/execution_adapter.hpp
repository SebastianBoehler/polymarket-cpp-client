#pragma once

#include "arb/arb_types.hpp"

namespace polymarket::arb
{

    class ExecutionAdapter
    {
    public:
        virtual ~ExecutionAdapter() = default;

        virtual bool live_trading_enabled() const = 0;
        virtual SubmitResult submit(const ExecutionPlan &plan) = 0;
        virtual FlattenReport flatten(const FlattenRequest &request,
                                      const PreparedMarket &market) = 0;
    };

} // namespace polymarket::arb
