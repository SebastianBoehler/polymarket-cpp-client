#include "orderbook_runtime.hpp"
#include "orderbook_stream.hpp"
#include "websocket_resilience.hpp"

#include <iostream>

namespace polymarket::detail
{
    void OrderbookRuntime::handle_message(const std::string &message,
                                          uint64_t websocket_generation)
    {
        const auto generation = stream_generation_.load();
        if (!stream_is_current(generation, websocket_generation) ||
            message.empty() || message == "PONG")
            return;
        try
        {
            if (message.find("\"tick_size_change\"") != std::string::npos)
            {
                const auto tick = parse_market_tick_size_change(message);
                if (!tick) return;
                std::string condition_id;
                {
                    std::lock_guard lock(subscriptions_mutex_);
                    const auto condition = token_to_condition_.find(tick->asset_id);
                    if (condition == token_to_condition_.end()) return;
                    condition_id = condition->second;
                }
                std::unique_lock lock(markets_mutex_);
                if (!stream_is_current(generation, websocket_generation)) return;
                const auto market = markets_.find(condition_id);
                if (market != markets_.end())
                    market->second->minimum_tick_size = tick->new_tick_size;
                return;
            }

            const auto events = parse_market_book_events(message);
            const auto received_ns = now_ns();
            for (const auto &event : events)
                handle_orderbook_update(event, received_ns, generation,
                                        websocket_generation);
        }
        catch (const std::exception &error)
        {
            if (!owner_active_.load(std::memory_order_acquire)) return;
            std::cerr << "[WS] Parse error: " << error.what() << '\n';
            websocket_.request_resnapshot(
                "invalid market data; resnapshot required");
        }
    }

    void OrderbookRuntime::handle_orderbook_update(
        const MarketBookEvent &event,
        uint64_t received_ns,
        uint64_t expected_generation,
        uint64_t expected_websocket_generation)
    {
        if (!stream_is_current(expected_generation,
                               expected_websocket_generation))
            return;
        const auto callbacks = callbacks_snapshot();
        std::string condition_id;
        std::optional<Orderbook> callback_book;
        {
            std::lock_guard subscription_lock(subscriptions_mutex_);
            if (!stream_is_current(expected_generation,
                                   expected_websocket_generation))
                return;
            const auto condition = token_to_condition_.find(event.asset_id);
            if (condition == token_to_condition_.end()) return;
            condition_id = condition->second;

            StreamBookTop top;
            {
                std::unique_lock book_lock(orderbooks_mutex_);
                const auto applied = apply_stream_book_event(
                    orderbooks_, snapshot_ready_, event, received_ns);
                if (!applied ||
                    !stream_is_current(expected_generation,
                                       expected_websocket_generation))
                    return;
                top = *applied;
                if (callbacks->update)
                    callback_book = orderbooks_.at(event.asset_id);
            }

            std::shared_lock market_lock(markets_mutex_);
            if (!stream_is_current(expected_generation,
                                   expected_websocket_generation))
                return;
            const auto found = markets_.find(condition_id);
            if (found == markets_.end()) return;
            auto &market = *found->second;
            if (event.asset_id == market.token_yes)
            {
                market.best_ask_yes.store(top.ask);
                market.best_ask_yes_size.store(top.ask_size);
                market.last_update_yes_ns.store(received_ns);
            }
            else if (event.asset_id == market.token_no)
            {
                market.best_ask_no.store(top.ask);
                market.best_ask_no_size.store(top.ask_size);
                market.last_update_no_ns.store(received_ns);
            }
            market.last_update_ns.store(received_ns);
            market.update_count++;
        }

        total_updates_++;
        if (callback_book &&
            stream_is_current(expected_generation,
                              expected_websocket_generation))
            callbacks->update(event.asset_id, *callback_book);
        if (!stream_is_current(expected_generation,
                               expected_websocket_generation))
            return;
        check_arb_opportunity(condition_id, expected_generation,
                              expected_websocket_generation);
    }

    void OrderbookRuntime::check_arb_opportunity(
        const std::string &condition_id,
        uint64_t expected_generation,
        uint64_t expected_websocket_generation)
    {
        if (!stream_is_current(expected_generation,
                               expected_websocket_generation))
            return;
        std::shared_ptr<LiveMarketState> live;
        MarketState market;
        {
            std::shared_lock lock(markets_mutex_);
            const auto found = markets_.find(condition_id);
            if (found == markets_.end()) return;
            live = found->second;
            market = snapshot_market(*live);
        }
        if (market.best_ask_yes <= 0.0 || market.best_ask_no <= 0.0)
            return;
        const double required_shares = market.minimum_order_size;
        if (!has_fresh_arb_depth(market, now_ns(),
                                 config_.max_book_age_ms * 1'000'000ULL,
                                 required_shares) ||
            market.combined() >= config_.trigger_combined ||
            !stream_is_current(expected_generation,
                               expected_websocket_generation))
            return;

        arb_opportunities_++;
        const auto callbacks = callbacks_snapshot();
        const auto admit = [this, expected_generation,
                            expected_websocket_generation]
        {
            return stream_is_current(expected_generation,
                                     expected_websocket_generation);
        };
        dispatch_arb_callbacks(
            std::move(live), market,
            {expected_generation, expected_websocket_generation},
            callbacks->arb, callbacks->arb_snapshot, callbacks->arb_permit,
            admit);
    }
}
