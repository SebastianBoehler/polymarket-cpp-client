#pragma once

#include "arb/execution_adapter.hpp"
#include "clob_client.hpp"
#include <memory>

namespace polymarket::arb
{

    class PolymarketExecutionAdapter : public ExecutionAdapter
    {
    public:
        PolymarketExecutionAdapter(std::shared_ptr<ClobClient> client, bool dry_run);

        bool live_trading_enabled() const override;
        SubmitResult submit(const ExecutionPlan &plan) override;
        FlattenReport flatten(const FlattenRequest &request,
                              const PreparedMarket &market) override;

    private:
        std::shared_ptr<ClobClient> client_;
        bool dry_run_;
    };

} // namespace polymarket::arb
