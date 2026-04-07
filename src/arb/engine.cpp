#include "arb/engine.hpp"
#include <thread>
#include <utility>

namespace polymarket::arb
{

    ArbEngine::ArbEngine(ArbConfig config, std::unique_ptr<ExecutionAdapter> adapter)
        : config_(std::move(config)),
          detector_(config_),
          risk_policy_(config_),
          adapter_(std::move(adapter)),
          runner_(config_, *adapter_, risk_policy_)
    {
    }

    ArbEngine::~ArbEngine()
    {
        if (execution_thread_.joinable())
        {
            execution_thread_.join();
        }
    }

    void ArbEngine::set_active_market(const PreparedMarket &market)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        active_market_ = market;
        risk_policy_.on_market_switched();
    }

    void ArbEngine::on_orderbooks(const Orderbook &yes_book, const Orderbook &no_book)
    {
        std::optional<PreparedMarket> active_market;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            active_market = active_market_;
        }
        if (!active_market.has_value())
        {
            return;
        }

        const auto signal = detector_.detect(*active_market, yes_book, no_book);
        if (!signal.has_value())
        {
            return;
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            counters_.opportunities_seen++;
        }

        if (execution_in_flight_.exchange(true))
        {
            std::lock_guard<std::mutex> lock(mutex_);
            counters_.opportunities_skipped++;
            return;
        }

        const auto gate = risk_policy_.allow(*signal);
        if (!gate.allowed)
        {
            execution_in_flight_.store(false);
            std::lock_guard<std::mutex> lock(mutex_);
            counters_.opportunities_skipped++;
            return;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        if (execution_thread_.joinable())
        {
            execution_thread_.join();
        }
        execution_thread_ = std::thread([this, signal = *signal, yes_book, no_book]()
                                        {
            auto report = runner_.execute(signal, yes_book, no_book);
            record_report(std::move(report));
            execution_in_flight_.store(false); });
    }

    ExecutionCounters ArbEngine::counters() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return counters_;
    }

    std::optional<ExecutionReport> ArbEngine::last_report() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return last_report_;
    }

    std::optional<PreparedMarket> ArbEngine::active_market() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return active_market_;
    }

    std::string ArbEngine::block_reason(uint64_t now_ns) const
    {
        if (execution_in_flight_.load())
        {
            return "execution_in_flight";
        }
        return risk_policy_.current_block_reason(now_ns);
    }

    void ArbEngine::record_report(ExecutionReport report)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!report.plan_created)
        {
            counters_.opportunities_skipped++;
        }
        if (report.submit_attempted)
        {
            counters_.orders_submitted++;
        }
        if (report.both_filled)
        {
            counters_.both_filled++;
        }
        if (report.one_filled)
        {
            counters_.one_filled++;
        }
        if (report.both_rejected)
        {
            counters_.both_rejected++;
        }
        last_report_ = std::move(report);
    }

} // namespace polymarket::arb
