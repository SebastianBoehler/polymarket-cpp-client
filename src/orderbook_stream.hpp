#pragma once

#include "orderbook.hpp"
#include "websocket_resilience.hpp"

#include <memory>
#include <functional>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace polymarket::detail
{
    using ArbCallbackAdmission = std::function<bool()>;

    struct StreamBookTop
    {
        double ask{0.0};
        double ask_size{0.0};
    };

    MarketState snapshot_market(const LiveMarketState &market);
    void reset_market_leg(LiveMarketState &market, const std::string &token_id);
    std::optional<StreamBookTop> apply_stream_book_event(
        std::unordered_map<std::string, Orderbook> &books,
        std::unordered_set<std::string> &snapshot_ready,
        const MarketBookEvent &event,
        uint64_t received_ns);
    void invalidate_stream_books(
        std::unordered_map<std::string, Orderbook> &books,
        std::unordered_set<std::string> &snapshot_ready);
    void dispatch_arb_callbacks(std::shared_ptr<LiveMarketState> market,
                                const MarketState &snapshot,
                                StreamGenerationPermit permit,
                                const ArbOpportunityCallback &legacy_callback,
                                const ArbOpportunitySnapshotCallback &snapshot_callback,
                                const ArbOpportunityPermitCallback &permit_callback,
                                const ArbCallbackAdmission &admit = {});
}
