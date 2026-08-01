#include "orderbook_runtime.hpp"
#include "orderbook_stream.hpp"
#include "orderbook_subscription.hpp"
#include "websocket_resilience.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace polymarket::detail
{
    namespace
    {
        std::vector<std::string> tracked_message(
            const std::vector<std::string> &tokens)
        {
            return tokens.empty()
                       ? std::vector<std::string>{}
                       : std::vector<std::string>{market_subscription_message(tokens)};
        }

        Config validated_config(Config config)
        {
            if (!std::isfinite(config.trigger_combined) ||
                config.trigger_combined <= 0.0 ||
                config.trigger_combined > 1.0)
            {
                throw std::invalid_argument(
                    "trigger_combined must be finite and in (0, 1]");
            }
            if (config.ws_ping_interval_ms <= 0)
            {
                throw std::invalid_argument(
                    "ws_ping_interval_ms must be positive");
            }
            if (config.ws_connect_timeout_ms <= 0)
            {
                throw std::invalid_argument(
                    "ws_connect_timeout_ms must be positive");
            }
            constexpr uint64_t nanoseconds_per_millisecond = 1'000'000ULL;
            if (config.max_book_age_ms == 0 ||
                config.max_book_age_ms >
                    std::numeric_limits<uint64_t>::max() /
                        nanoseconds_per_millisecond)
            {
                throw std::invalid_argument(
                    "max_book_age_ms is outside the supported range");
            }
            return config;
        }
    }

    std::shared_ptr<OrderbookRuntime> OrderbookRuntime::create(
        const Config &config)
    {
        auto runtime = std::shared_ptr<OrderbookRuntime>(
            new OrderbookRuntime(config));
        runtime->bind_websocket_callbacks();
        return runtime;
    }

    OrderbookRuntime::OrderbookRuntime(const Config &config)
        : config_(validated_config(config))
    {
        websocket_.set_url(config_.clob_ws_url);
        websocket_.set_ping_interval_ms(config_.ws_ping_interval_ms);
        websocket_.set_auto_reconnect(true);
    }

    OrderbookRuntime::~OrderbookRuntime()
    {
        shutdown();
    }

    void OrderbookRuntime::bind_websocket_callbacks()
    {
        std::weak_ptr<OrderbookRuntime> weak = shared_from_this();
        websocket_.on_sequenced_message(
            [weak](const std::string &message, uint64_t generation)
            {
                if (auto runtime = weak.lock())
                    runtime->handle_message(message, generation);
            });
        websocket_.on_stream_gap(
            [weak](uint64_t)
            {
                if (auto runtime = weak.lock())
                    runtime->invalidate_stream_state();
            });
        websocket_.on_connect([]
                              { std::cout << "[WS] Connected to CLOB market stream\n"; });
        websocket_.on_disconnect([]
                                 { std::cout << "[WS] Disconnected from CLOB market stream\n"; });
        websocket_.on_error([](const std::string &error)
                            { std::cerr << "[WS] Error: " << error << '\n'; });
    }

    void OrderbookRuntime::shutdown()
    {
        if (shutdown_started_.exchange(true)) return;
        owner_active_.store(false, std::memory_order_release);
        deactivate_stream();
        clear_callbacks();
        websocket_.stop();
    }

    void OrderbookRuntime::subscribe(const std::vector<MarketState> &markets)
    {
        for (const auto &market : markets)
            subscribe(market);
    }

    void OrderbookRuntime::subscribe(const MarketState &market)
    {
        if (!owner_active_.load(std::memory_order_acquire)) return;
        {
            std::unique_lock lock(markets_mutex_);
            auto live = std::make_shared<LiveMarketState>(market);
            reset_market_leg(*live, live->token_yes);
            reset_market_leg(*live, live->token_no);
            markets_[market.condition_id] = std::move(live);
        }

        std::vector<std::string> added;
        {
            std::lock_guard lock(subscriptions_mutex_);
            for (const auto &token : {market.token_yes, market.token_no})
            {
                if (token.empty() ||
                    std::find(subscribed_tokens_.begin(), subscribed_tokens_.end(),
                              token) != subscribed_tokens_.end())
                    continue;
                subscribed_tokens_.push_back(token);
                token_to_condition_[token] = market.condition_id;
                added.push_back(token);
            }
            websocket_.replace_subscriptions(tracked_message(subscribed_tokens_));
        }
        if (websocket_.is_connected() && !added.empty() &&
            !websocket_.send(market_subscription_update_message(added, true)))
            recover_failed_subscription_send(websocket_);
        std::cout << "[OrderbookManager] Tracking " << market.slug << '\n';
    }

    void OrderbookRuntime::unsubscribe(const std::string &token_id)
    {
        if (!owner_active_.load(std::memory_order_acquire)) return;
        bool removed = false;
        std::string condition_id;
        {
            std::lock_guard lock(subscriptions_mutex_);
            const auto token = std::find(subscribed_tokens_.begin(),
                                         subscribed_tokens_.end(), token_id);
            if (token != subscribed_tokens_.end())
            {
                subscribed_tokens_.erase(token);
                condition_id = token_to_condition_[token_id];
                token_to_condition_.erase(token_id);
                removed = true;
            }
            websocket_.replace_subscriptions(tracked_message(subscribed_tokens_));
        }
        if (removed && websocket_.is_connected() &&
            !websocket_.send(market_subscription_update_message({token_id}, false)))
            recover_failed_subscription_send(websocket_);
        {
            std::unique_lock lock(orderbooks_mutex_);
            orderbooks_.erase(token_id);
            snapshot_ready_.erase(token_id);
        }
        if (!removed) return;
        std::shared_lock lock(markets_mutex_);
        const auto market = markets_.find(condition_id);
        if (market != markets_.end()) reset_market_leg(*market->second, token_id);
    }

    void OrderbookRuntime::unsubscribe_all()
    {
        if (!owner_active_.load(std::memory_order_acquire)) return;
        std::vector<std::string> removed;
        {
            std::lock_guard lock(subscriptions_mutex_);
            removed.swap(subscribed_tokens_);
            token_to_condition_.clear();
            websocket_.replace_subscriptions({});
        }
        if (websocket_.is_connected() && !removed.empty() &&
            !websocket_.send(market_subscription_update_message(removed, false)))
            recover_failed_subscription_send(websocket_);
        {
            std::unique_lock lock(orderbooks_mutex_);
            invalidate_stream_books(orderbooks_, snapshot_ready_);
        }
        std::unique_lock lock(markets_mutex_);
        markets_.clear();
    }

    std::optional<Orderbook> OrderbookRuntime::get_orderbook(
        const std::string &token_id) const
    {
        std::shared_lock lock(orderbooks_mutex_);
        const auto book = orderbooks_.find(token_id);
        return book == orderbooks_.end() ? std::nullopt
                                         : std::optional<Orderbook>(book->second);
    }

    MarketState OrderbookRuntime::get_market(const std::string &condition_id) const
    {
        std::shared_lock lock(markets_mutex_);
        const auto market = markets_.find(condition_id);
        return market == markets_.end() ? MarketState{}
                                        : snapshot_market(*market->second);
    }

    void OrderbookRuntime::on_orderbook_update(OrderbookUpdateCallback callback)
    {
        update_callbacks([callback = std::move(callback)](auto &callbacks) mutable
                         { callbacks.update = std::move(callback); });
    }

    void OrderbookRuntime::on_arb_opportunity(ArbOpportunityCallback callback)
    {
        update_callbacks([callback = std::move(callback)](auto &callbacks) mutable
                         { callbacks.arb = std::move(callback); });
    }

    void OrderbookRuntime::on_arb_opportunity_snapshot(
        ArbOpportunitySnapshotCallback callback)
    {
        update_callbacks([callback = std::move(callback)](auto &callbacks) mutable
                         { callbacks.arb_snapshot = std::move(callback); });
    }

    void OrderbookRuntime::on_arb_opportunity_with_permit(
        ArbOpportunityPermitCallback callback)
    {
        update_callbacks([callback = std::move(callback)](auto &callbacks) mutable
                         { callbacks.arb_permit = std::move(callback); });
    }

    void OrderbookRuntime::clear_callbacks()
    {
        std::lock_guard lock(callback_update_mutex_);
        std::shared_ptr<const OrderbookCallbacks> empty =
            std::make_shared<OrderbookCallbacks>();
        std::atomic_store_explicit(&callbacks_, std::move(empty),
                                   std::memory_order_release);
    }

    uint64_t OrderbookRuntime::total_updates() const
    {
        return total_updates_.load();
    }

    uint64_t OrderbookRuntime::arb_opportunities() const
    {
        return arb_opportunities_.load();
    }
}
