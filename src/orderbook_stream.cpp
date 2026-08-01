#include "orderbook_stream.hpp"

#include <algorithm>

namespace polymarket::detail
{
    MarketState snapshot_market(const LiveMarketState &live)
    {
        MarketState state;
        state.slug = live.slug;
        state.title = live.title;
        state.symbol = live.symbol;
        state.condition_id = live.condition_id;
        state.token_yes = live.token_yes;
        state.token_no = live.token_no;
        state.best_ask_yes = live.best_ask_yes.load();
        state.best_ask_no = live.best_ask_no.load();
        state.best_ask_yes_size = live.best_ask_yes_size.load();
        state.best_ask_no_size = live.best_ask_no_size.load();
        state.minimum_order_size = live.minimum_order_size;
        state.minimum_tick_size = live.minimum_tick_size;
        state.end_time_ms = live.end_time_ms;
        state.neg_risk = live.neg_risk;
        state.fees_enabled = live.fees_enabled;
        state.fee_rate = live.fee_rate;
        state.fee_exponent = live.fee_exponent;
        state.last_update_ns = live.last_update_ns.load();
        state.last_update_yes_ns = live.last_update_yes_ns.load();
        state.last_update_no_ns = live.last_update_no_ns.load();
        state.update_count = live.update_count.load();
        return state;
    }

    void reset_market_leg(LiveMarketState &market, const std::string &token_id)
    {
        if (token_id == market.token_yes)
        {
            market.best_ask_yes.store(0.0);
            market.best_ask_yes_size.store(0.0);
            market.last_update_yes_ns.store(0);
        }
        else if (token_id == market.token_no)
        {
            market.best_ask_no.store(0.0);
            market.best_ask_no_size.store(0.0);
            market.last_update_no_ns.store(0);
        }
        market.last_update_ns.store(std::max(market.last_update_yes_ns.load(),
                                             market.last_update_no_ns.load()));
    }

    std::optional<StreamBookTop> apply_stream_book_event(
        std::unordered_map<std::string, Orderbook> &books,
        std::unordered_set<std::string> &snapshot_ready,
        const MarketBookEvent &event,
        uint64_t received_ns)
    {
        if (!event.snapshot && snapshot_ready.count(event.asset_id) == 0)
            return std::nullopt;
        auto &book = books[event.asset_id];
        apply_market_book_event(book, event, received_ns);
        if (event.snapshot) snapshot_ready.insert(event.asset_id);
        return book.asks.empty()
                   ? StreamBookTop{}
                   : StreamBookTop{book.asks.front().price,
                                   book.asks.front().size};
    }

    void invalidate_stream_books(
        std::unordered_map<std::string, Orderbook> &books,
        std::unordered_set<std::string> &snapshot_ready)
    {
        books.clear();
        snapshot_ready.clear();
    }

    void dispatch_arb_callbacks(
        std::shared_ptr<LiveMarketState> market,
        const MarketState &snapshot,
        StreamGenerationPermit permit,
        const ArbOpportunityCallback &legacy_callback,
        const ArbOpportunitySnapshotCallback &snapshot_callback,
        const ArbOpportunityPermitCallback &permit_callback,
        const ArbCallbackAdmission &admit)
    {
        const auto admitted = [&] { return !admit || admit(); };
        if (legacy_callback && market && admitted())
            legacy_callback(*market, snapshot.combined());
        if (snapshot_callback && admitted())
            snapshot_callback(snapshot, snapshot.combined());
        if (permit_callback && admitted())
            permit_callback(snapshot, snapshot.combined(), permit);
    }
}
