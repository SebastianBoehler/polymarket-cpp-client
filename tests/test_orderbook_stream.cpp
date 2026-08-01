#include "orderbook.hpp"
#include "orderbook_subscription.hpp"
#include "orderbook_stream.hpp"
#include "websocket_resilience.hpp"

#include <cmath>
#include <functional>
#include <iostream>
#include <memory>
#include <limits>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

using namespace polymarket;

static_assert(std::is_same_v<
              ArbOpportunityCallback,
              std::function<void(const LiveMarketState &, double)>>,
              "legacy arbitrage callback must remain source-compatible");

namespace
{
    bool close_to(double actual, double expected)
    {
        return std::abs(actual - expected) < 1e-9;
    }

    bool check(bool value, const char *name)
    {
        if (!value)
        {
            std::cerr << "failed: " << name << '\n';
        }
        return value;
    }
}

int main()
{
    WebSocketClient subscription_transport;
    const auto generation_before_failed_send =
        subscription_transport.stream_generation();
    detail::recover_failed_subscription_send(subscription_transport);
    if (!check(subscription_transport.stream_generation() ==
                   generation_before_failed_send + 1,
               "failed subscription send invalidates the stream"))
    {
        return 1;
    }

    const auto snapshots = detail::parse_market_book_events(
        R"([{"event_type":"book","market":"condition-1","asset_id":"yes","timestamp":"1782753357257","bids":[{"price":"0.40","size":"8"}],"asks":[{"price":"0.46","size":"5"},{"price":"0.45","size":"20"}]},{"event_type":"book","market":"condition-1","asset_id":"no","timestamp":"1782753357257","bids":[],"asks":[{"price":"0.48","size":"30"}]}])");
    if (!check(snapshots.size() == 2, "snapshot array is flattened") ||
        !check(snapshots[0].snapshot, "book event is a snapshot") ||
        !check(snapshots[0].asset_id == "yes", "snapshot asset id"))
    {
        return 1;
    }

    for (const auto *invalid_snapshot : {
             R"({"event_type":"book","asset_id":"yes","asks":[]})",
             R"({"event_type":"book","asset_id":"yes","bids":[]})",
             R"({"event_type":"book","asset_id":"yes","bids":{},"asks":[]})",
             R"({"event_type":"book","asset_id":"yes","bids":[],"asks":"bad"})",
             R"({"event_type":"book","asset_id":"yes","bids":[{"price":"0.4","size":"1"},{"price":"0.4","size":"2"}],"asks":[]})"})
    {
        bool rejected_snapshot = false;
        try
        {
            (void)detail::parse_market_book_events(invalid_snapshot);
        }
        catch (const std::invalid_argument &)
        {
            rejected_snapshot = true;
        }
        if (!check(rejected_snapshot,
                   "malformed or duplicate snapshot levels are rejected"))
        {
            return 1;
        }
    }

    Orderbook yes;
    detail::apply_market_book_event(yes, snapshots[0], 1000);
    if (!check(close_to(yes.best_ask(), 0.45), "snapshot best ask") ||
        !check(close_to(yes.best_ask_size(), 20.0), "snapshot best ask size") ||
        !check(yes.timestamp_ns == 1000, "snapshot uses receipt time"))
    {
        return 1;
    }

    const auto changes = detail::parse_market_book_events(
        R"({"event_type":"price_change","market":"condition-1","price_changes":[{"asset_id":"yes","price":"0.45","size":"0","side":"SELL","hash":"a"},{"asset_id":"yes","price":"0.44","size":"12","side":"SELL","hash":"b"},{"asset_id":"yes","price":"0.40","size":"9","side":"BUY","hash":"c"}],"timestamp":"1782753358257"})");
    if (!check(changes.size() == 1, "same-asset changes are grouped"))
    {
        return 1;
    }
    detail::apply_market_book_event(yes, changes[0], 2000);
    if (!check(close_to(yes.best_ask(), 0.44), "price change removes and inserts asks") ||
        !check(close_to(yes.asks.front().price, 0.44), "delta keeps asks sorted at the hot-path top") ||
        !check(close_to(yes.best_ask_size(), 12.0), "price change ask size") ||
        !check(close_to(yes.best_bid_size(), 9.0), "price change updates bid size") ||
        !check(close_to(yes.bids.front().price, 0.40), "delta keeps bids sorted"))
    {
        return 1;
    }

    std::unordered_map<std::string, Orderbook> guarded_books;
    std::unordered_set<std::string> snapshot_ready;
    if (!check(detail::apply_stream_book_event(guarded_books, snapshot_ready,
                                                snapshots[0], 1000)
                   .has_value(),
               "snapshot enables a stream leg") ||
        !check(detail::apply_stream_book_event(guarded_books, snapshot_ready,
                                                snapshots[1], 1000)
                   .has_value(),
               "second snapshot enables its stream leg"))
    {
        return 1;
    }
    detail::invalidate_stream_books(guarded_books, snapshot_ready);
    if (!check(!detail::apply_stream_book_event(guarded_books, snapshot_ready,
                                                 changes[0], 2000)
                    .has_value(),
               "delta after a stream gap is ignored") ||
        !check(guarded_books.empty(), "gap leaves no stale book") ||
        !check(detail::apply_stream_book_event(guarded_books, snapshot_ready,
                                                snapshots[0], 3000)
                   .has_value(),
               "fresh snapshot re-enables one leg") ||
        !check(snapshot_ready.count("yes") == 1 && snapshot_ready.count("no") == 0,
               "opposite leg remains gated until its own snapshot"))
    {
        return 1;
    }

    const auto tick = detail::parse_market_tick_size_change(
        R"({"event_type":"tick_size_change","asset_id":"yes","old_tick_size":"0.01","new_tick_size":"0.001","timestamp":"1782753359257"})");
    if (!check(tick.has_value(), "tick-size event parsed") ||
        !check(tick->asset_id == "yes", "tick-size asset id") ||
        !check(tick->new_tick_size == "0.001", "new live tick size"))
    {
        return 1;
    }
    for (const auto *invalid_tick : {"0.02", "nan", "0.0010", ""})
    {
        const auto event = std::string{
                               R"({"event_type":"tick_size_change","asset_id":"yes","new_tick_size":")"} +
                           invalid_tick + R"("})";
        if (!check(!detail::parse_market_tick_size_change(event),
                   "unsupported live tick size is rejected"))
        {
            return 1;
        }
    }

    bool rejected_nan = false;
    try
    {
        (void)detail::parse_market_book_events(
            R"({"event_type":"book","asset_id":"yes","bids":[],"asks":[{"price":"0.45","size":"nan"}]})");
    }
    catch (const std::invalid_argument &)
    {
        rejected_nan = true;
    }
    if (!check(rejected_nan, "non-finite wire depth is rejected"))
    {
        return 1;
    }

    for (const auto *invalid_change : {
             R"({"event_type":"price_change","price_changes":[{"asset_id":"yes","price":"0.10","size":"50","side":"HOLD"}]})",
             R"({"event_type":"price_change","price_changes":[{"asset_id":"yes","price":"0.10","size":"50"}]})"})
    {
        bool rejected_side = false;
        try
        {
            (void)detail::parse_market_book_events(invalid_change);
        }
        catch (const std::invalid_argument &)
        {
            rejected_side = true;
        }
        if (!check(rejected_side, "unknown or missing price-change side is rejected"))
        {
            return 1;
        }
    }

    MarketState market;
    market.best_ask_yes = 0.40;
    market.best_ask_no = 0.50;
    market.best_ask_yes_size = 10.0;
    market.best_ask_no_size = 10.0;
    market.last_update_yes_ns = 8'500'000'000ULL;
    market.last_update_no_ns = 9'000'000'000ULL;
    market.last_update_ns = 9'000'000'000ULL;

    LiveMarketState live(market);
    if (!check(live.last_update_ns.load() == 9'000'000'000ULL,
               "legacy latest timestamp is copied"))
    {
        return 1;
    }

    if (!check(has_fresh_arb_depth(market, 10'000'000'000ULL, 2'000'000'000ULL, 10.0),
               "both books fresh with enough depth") ||
        !check(!has_fresh_arb_depth(market, 10'600'000'000ULL, 2'000'000'000ULL, 10.0),
               "stale yes book blocks callback"))
    {
        return 1;
    }
    market.last_update_yes_ns = 10'000'000'000ULL;
    market.best_ask_no_size = 9.99;
    if (!check(!has_fresh_arb_depth(market, 10'000'000'000ULL, 2'000'000'000ULL, 10.0),
               "insufficient no depth blocks callback"))
    {
        return 1;
    }
    market.best_ask_no_size = std::numeric_limits<double>::quiet_NaN();
    if (!check(!has_fresh_arb_depth(market, 10'000'000'000ULL,
                                    2'000'000'000ULL, 1.0),
               "non-finite market depth is never executable"))
    {
        return 1;
    }

    market.token_yes = "yes";
    market.token_no = "no";
    market.best_ask_yes = 0.45;
    market.best_ask_yes_size = 12.0;
    market.best_ask_no = 0.48;
    market.best_ask_no_size = 15.0;
    market.last_update_yes_ns = 11'000'000'000ULL;
    market.last_update_no_ns = 12'000'000'000ULL;
    market.last_update_ns = 12'000'000'000ULL;
    auto shared_market = std::make_shared<LiveMarketState>(market);
    detail::reset_market_leg(*shared_market, "yes");
    const auto reset = detail::snapshot_market(*shared_market);
    if (!check(reset.best_ask_yes == 0.0 && reset.best_ask_yes_size == 0.0 &&
                   reset.last_update_yes_ns == 0,
               "unsubscribe clears the removed leg") ||
        !check(reset.last_update_ns == reset.last_update_no_ns,
               "legacy timestamp tracks the remaining leg") ||
        !check(!has_fresh_arb_depth(reset, 12'000'000'000ULL, 2'000'000'000ULL, 1.0),
               "an incomplete subscription cannot trigger arbitrage"))
    {
        return 1;
    }

    std::weak_ptr<LiveMarketState> weak_market = shared_market;
    bool legacy_saw_live_market = false;
    bool snapshot_called = false;
    bool permit_called = false;
    ArbOpportunityCallback legacy = [&](const LiveMarketState &value, double)
    {
        shared_market.reset();
        legacy_saw_live_market = !weak_market.expired() && value.token_no == "no";
    };
    ArbOpportunitySnapshotCallback snapshot = [&](const MarketState &, double)
    {
        snapshot_called = true;
    };
    ArbOpportunityPermitCallback permit =
        [&](const MarketState &, double, StreamGenerationPermit value)
    {
        permit_called = value.orderbook_generation == 3 &&
                        value.websocket_generation == 7;
    };
    detail::dispatch_arb_callbacks(shared_market, reset, {3, 7},
                                   legacy, snapshot, permit);
    if (!check(legacy_saw_live_market, "legacy callback retains market lifetime") ||
        !check(snapshot_called, "snapshot callback is dispatched") ||
        !check(permit_called, "stream permit reaches the hardened callback") ||
        !check(weak_market.expired(), "dispatch releases its lifetime guard afterward"))
    {
        return 1;
    }

    return 0;
}
