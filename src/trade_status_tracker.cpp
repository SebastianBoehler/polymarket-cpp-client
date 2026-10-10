#include "polymarket/trade_status_tracker.hpp"
#include "trade_status.hpp"

#include <stdexcept>

namespace polymarket
{
    namespace
    {
        Trade to_trade(const UserTradeEvent &event)
        {
            Trade trade;
            trade.id = event.id;
            trade.taker_order_id = event.taker_order_id;
            trade.market = event.market;
            trade.asset_id = event.asset_id;
            trade.side = event.side;
            trade.size = event.size;
            trade.fee_rate_bps = event.fee_rate_bps;
            trade.price = event.price;
            trade.status = event.status;
            trade.match_time = event.match_time;
            trade.last_update = event.last_update;
            trade.outcome = event.outcome;
            trade.bucket_index = event.bucket_index.value_or(0);
            trade.owner = event.owner;
            trade.maker_address = event.maker_address;
            trade.maker_orders = event.maker_orders;
            trade.transaction_hash = event.transaction_hash;
            trade.trader_side = event.trader_side;
            return trade;
        }
    } // namespace

    TradeStatusTracker::TradeStatusTracker(std::size_t capacity) : capacity_(capacity)
    {
        if (capacity_ == 0)
            throw std::invalid_argument("trade status tracker capacity must be positive");
    }

    void TradeStatusTracker::record(const UserTradeEvent &trade)
    {
        if (trade.id.empty()) return;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto existing = trades_.find(trade.id);
            if (existing == trades_.end())
            {
                trades_.emplace(trade.id, to_trade(trade));
                arrival_order_.push_back(trade.id);
                if (trades_.size() > capacity_)
                {
                    trades_.erase(arrival_order_.front());
                    arrival_order_.pop_front();
                }
            }
            else
            {
                if (detail::is_settled_trade_status(existing->second.status) &&
                    !detail::is_settled_trade_status(trade.status))
                    return;
                existing->second = to_trade(trade);
            }
            ++version_;
        }
        changed_.notify_all();
    }

    void TradeStatusTracker::mark_recovered()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ++recoveries_;
            ++version_;
        }
        changed_.notify_all();
    }

    std::optional<Trade> TradeStatusTracker::latest(const std::string &trade_id) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = trades_.find(trade_id);
        if (found == trades_.end()) return std::nullopt;
        return found->second;
    }

    std::uint64_t TradeStatusTracker::version() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return version_;
    }

    std::uint64_t TradeStatusTracker::recoveries() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return recoveries_;
    }

    void TradeStatusTracker::wait_for_change(std::uint64_t seen_version,
                                             std::chrono::steady_clock::time_point deadline) const
    {
        std::unique_lock<std::mutex> lock(mutex_);
        changed_.wait_until(lock, deadline, [&] { return version_ != seen_version; });
    }
} // namespace polymarket
