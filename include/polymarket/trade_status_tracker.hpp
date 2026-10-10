#pragma once

#include "polymarket/clob_types.hpp"
#include "polymarket/user_stream.hpp"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace polymarket
{
    // Latest status of each trade reported on a UserStream. Feed it from the
    // stream callbacks and pass it to ClobClient::wait_for_order_fill_settlement,
    // which then wakes on pushed updates instead of polling REST:
    //
    //   stream.on_trade([&](const UserTradeEvent &trade) { tracker.record(trade); });
    //   stream.on_stream_recovered([&] { tracker.mark_recovered(); });
    //
    // Keep the tracker alive until the stream is stopped. It remembers the most
    // recent `capacity` trades; forgotten trades are resolved through REST.
    // CONFIRMED and FAILED are final, so a later earlier-stage event never
    // replaces them.
    class TradeStatusTracker
    {
      public:
        explicit TradeStatusTracker(std::size_t capacity = 10000);

        void record(const UserTradeEvent &trade);
        // Call once the stream has resubscribed after a gap; waiting calls
        // then reconcile pending trades through REST.
        void mark_recovered();

        std::optional<Trade> latest(const std::string &trade_id) const;
        // Changes on every recorded update and every recovery.
        std::uint64_t version() const;
        std::uint64_t recoveries() const;
        // Returns once version() differs from seen_version or at the deadline.
        void wait_for_change(std::uint64_t seen_version,
                             std::chrono::steady_clock::time_point deadline) const;

      private:
        std::size_t capacity_;
        mutable std::mutex mutex_;
        mutable std::condition_variable changed_;
        std::unordered_map<std::string, Trade> trades_;
        std::deque<std::string> arrival_order_;
        std::uint64_t version_{0};
        std::uint64_t recoveries_{0};
    };
} // namespace polymarket
