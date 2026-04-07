#pragma once

#include "arb/arb_types.hpp"
#include "arb/execution_runner.hpp"
#include "arb/risk_policy.hpp"
#include "arb/signal_detector.hpp"
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace polymarket::arb
{

    class ArbEngine
    {
    public:
        ArbEngine(ArbConfig config, std::unique_ptr<ExecutionAdapter> adapter);
        ~ArbEngine();

        void set_active_market(const PreparedMarket &market);
        void on_orderbooks(const Orderbook &yes_book, const Orderbook &no_book);

        ExecutionCounters counters() const;
        std::optional<ExecutionReport> last_report() const;
        std::optional<PreparedMarket> active_market() const;
        std::string block_reason(uint64_t now_ns) const;

    private:
        void record_report(ExecutionReport report);

        ArbConfig config_;
        SignalDetector detector_;
        RiskPolicy risk_policy_;
        std::unique_ptr<ExecutionAdapter> adapter_;
        ExecutionRunner runner_;
        mutable std::mutex mutex_;
        std::optional<PreparedMarket> active_market_;
        std::optional<ExecutionReport> last_report_;
        ExecutionCounters counters_;
        std::atomic<bool> execution_in_flight_{false};
        std::thread execution_thread_;
    };

} // namespace polymarket::arb
