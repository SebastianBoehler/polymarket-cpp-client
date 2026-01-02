#include "orderbook.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
#include <algorithm>

using json = nlohmann::json;

namespace polymarket
{

    OrderbookManager::OrderbookManager(const Config &config)
        : config_(config)
    {
        // Use real-time data WebSocket endpoint (same as @polymarket/real-time-data-client)
        ws_.set_url(config_.rtds_ws_url);
        ws_.set_ping_interval_ms(config_.ws_ping_interval_ms);
        ws_.set_auto_reconnect(true);

        // Set up WebSocket callbacks
        ws_.on_message([this](const std::string &msg)
                       { handle_message(msg); });

        ws_.on_connect([this]()
                       {
        std::cout << "[WS] Connected to orderbook stream" << std::endl;
        send_subscribe_message(); });

        ws_.on_disconnect([this]()
                          { std::cout << "[WS] Disconnected from orderbook stream" << std::endl; });

        ws_.on_error([](const std::string &error)
                     { std::cerr << "[WS] Error: " << error << std::endl; });
    }

    OrderbookManager::~OrderbookManager()
    {
        stop();
    }

    void OrderbookManager::subscribe(const std::vector<MarketState> &markets)
    {
        for (const auto &market : markets)
        {
            subscribe(market);
        }
    }

    void OrderbookManager::subscribe(const MarketState &market)
    {
        {
            std::unique_lock<std::shared_mutex> lock(markets_mutex_);
            markets_[market.condition_id] = std::make_unique<LiveMarketState>(market);
        }

        // Map tokens to condition
        token_to_condition_[market.token_yes] = market.condition_id;
        token_to_condition_[market.token_no] = market.condition_id;

        // Add to subscribed tokens
        subscribed_tokens_.push_back(market.token_yes);
        subscribed_tokens_.push_back(market.token_no);

        std::cout << "[OrderbookManager] Subscribed to market: " << market.slug
                  << " (YES: " << market.token_yes.substr(0, 16) << "...)" << std::endl;
    }

    void OrderbookManager::unsubscribe(const std::string &token_id)
    {
        auto it = std::find(subscribed_tokens_.begin(), subscribed_tokens_.end(), token_id);
        if (it != subscribed_tokens_.end())
        {
            subscribed_tokens_.erase(it);
        }

        std::unique_lock<std::shared_mutex> lock(orderbooks_mutex_);
        orderbooks_.erase(token_id);
    }

    void OrderbookManager::unsubscribe_all()
    {
        subscribed_tokens_.clear();

        {
            std::unique_lock<std::shared_mutex> lock(orderbooks_mutex_);
            orderbooks_.clear();
        }

        {
            std::unique_lock<std::shared_mutex> lock(markets_mutex_);
            markets_.clear();
        }

        token_to_condition_.clear();
    }

    std::optional<Orderbook> OrderbookManager::get_orderbook(const std::string &token_id) const
    {
        std::shared_lock<std::shared_mutex> lock(orderbooks_mutex_);
        auto it = orderbooks_.find(token_id);
        if (it != orderbooks_.end())
        {
            return it->second;
        }
        return std::nullopt;
    }

    MarketState OrderbookManager::get_market(const std::string &condition_id) const
    {
        std::shared_lock<std::shared_mutex> lock(markets_mutex_);
        auto it = markets_.find(condition_id);
        if (it != markets_.end())
        {
            const auto &live = it->second;
            MarketState state;
            state.slug = live->slug;
            state.title = live->title;
            state.symbol = live->symbol;
            state.condition_id = live->condition_id;
            state.token_yes = live->token_yes;
            state.token_no = live->token_no;
            state.best_ask_yes = live->best_ask_yes.load();
            state.best_ask_no = live->best_ask_no.load();
            return state;
        }
        return MarketState{};
    }

    void OrderbookManager::on_orderbook_update(OrderbookUpdateCallback callback)
    {
        on_update_cb_ = std::move(callback);
    }

    void OrderbookManager::on_arb_opportunity(ArbOpportunityCallback callback)
    {
        on_arb_cb_ = std::move(callback);
    }

    bool OrderbookManager::connect()
    {
        return ws_.connect();
    }

    void OrderbookManager::disconnect()
    {
        ws_.disconnect();
    }

    bool OrderbookManager::is_connected() const
    {
        return ws_.is_connected();
    }

    void OrderbookManager::run()
    {
        ws_.run();
    }

    void OrderbookManager::stop()
    {
        ws_.stop();
    }

    void OrderbookManager::send_subscribe_message()
    {
        if (subscribed_tokens_.empty())
        {
            return;
        }

        // Build subscription message for Polymarket Real-Time Data WebSocket
        // Format matches @polymarket/real-time-data-client:
        // {"action": "subscribe", "subscriptions": [{"topic": "clob_market", "type": "agg_orderbook", "filters": "[token1,token2]"}]}
        json subscribe_msg;
        subscribe_msg["action"] = "subscribe";

        json subscription;
        subscription["topic"] = "clob_market";
        subscription["type"] = "agg_orderbook";
        subscription["filters"] = json(subscribed_tokens_).dump(); // JSON array as string

        subscribe_msg["subscriptions"] = json::array({subscription});

        std::string msg = subscribe_msg.dump();
        std::cout << "[WS] Sending subscribe: " << subscribed_tokens_.size() << " tokens" << std::endl;

        ws_.send(msg);
    }

    void OrderbookManager::handle_message(const std::string &message)
    {
        // Skip empty messages
        if (message.empty() || message == "{}")
        {
            return;
        }

        try
        {
            auto j = json::parse(message);

            // Handle Polymarket Real-Time Data format:
            // {"topic": "clob_market", "type": "agg_orderbook", "payload": {"asset_id": "...", "asks": [...], "bids": [...]}}

            // Check for real-time data format
            if (j.contains("topic") && j.contains("type") && j.contains("payload"))
            {
                std::string topic = j["topic"].get<std::string>();
                std::string type = j["type"].get<std::string>();

                if (topic == "clob_market" && type == "agg_orderbook")
                {
                    auto &payload = j["payload"];
                    if (!payload.contains("asset_id"))
                    {
                        return;
                    }

                    std::string asset_id = payload["asset_id"].get<std::string>();

                    Orderbook book;
                    book.asset_id = asset_id;
                    book.timestamp_ns = now_ns();

                    // Parse asks
                    if (payload.contains("asks") && payload["asks"].is_array())
                    {
                        for (const auto &ask : payload["asks"])
                        {
                            PriceLevel level;
                            level.price = std::stod(ask["price"].get<std::string>());
                            level.size = std::stod(ask["size"].get<std::string>());
                            book.asks.push_back(level);
                        }
                    }

                    // Parse bids
                    if (payload.contains("bids") && payload["bids"].is_array())
                    {
                        for (const auto &bid : payload["bids"])
                        {
                            PriceLevel level;
                            level.price = std::stod(bid["price"].get<std::string>());
                            level.size = std::stod(bid["size"].get<std::string>());
                            book.bids.push_back(level);
                        }
                    }

                    handle_orderbook_update(asset_id, book);
                    return;
                }
            }

            // Legacy format: {"event_type": "book", "asset_id": "...", "bids": [...], "asks": [...]}
            if (!j.contains("event_type"))
            {
                return;
            }

            std::string event_type = j["event_type"].get<std::string>();

            if (event_type == "book" || event_type == "price_change")
            {
                if (!j.contains("asset_id"))
                {
                    return;
                }

                std::string asset_id = j["asset_id"].get<std::string>();

                Orderbook book;
                book.asset_id = asset_id;
                book.timestamp_ns = now_ns();

                // Parse bids
                if (j.contains("bids") && j["bids"].is_array())
                {
                    for (const auto &bid : j["bids"])
                    {
                        PriceLevel level;
                        if (bid.contains("price"))
                        {
                            if (bid["price"].is_string())
                            {
                                level.price = std::stod(bid["price"].get<std::string>());
                            }
                            else
                            {
                                level.price = bid["price"].get<double>();
                            }
                        }
                        if (bid.contains("size"))
                        {
                            if (bid["size"].is_string())
                            {
                                level.size = std::stod(bid["size"].get<std::string>());
                            }
                            else
                            {
                                level.size = bid["size"].get<double>();
                            }
                        }
                        book.bids.push_back(level);
                    }
                    // Sort bids descending by price
                    std::sort(book.bids.begin(), book.bids.end(),
                              [](const PriceLevel &a, const PriceLevel &b)
                              { return a.price > b.price; });
                }

                // Parse asks
                if (j.contains("asks") && j["asks"].is_array())
                {
                    for (const auto &ask : j["asks"])
                    {
                        PriceLevel level;
                        if (ask.contains("price"))
                        {
                            if (ask["price"].is_string())
                            {
                                level.price = std::stod(ask["price"].get<std::string>());
                            }
                            else
                            {
                                level.price = ask["price"].get<double>();
                            }
                        }
                        if (ask.contains("size"))
                        {
                            if (ask["size"].is_string())
                            {
                                level.size = std::stod(ask["size"].get<std::string>());
                            }
                            else
                            {
                                level.size = ask["size"].get<double>();
                            }
                        }
                        book.asks.push_back(level);
                    }
                    // Sort asks ascending by price
                    std::sort(book.asks.begin(), book.asks.end(),
                              [](const PriceLevel &a, const PriceLevel &b)
                              { return a.price < b.price; });
                }

                handle_orderbook_update(asset_id, book);
            }
        }
        catch (const std::exception &e)
        {
            std::cerr << "[WS] Parse error: " << e.what() << std::endl;
        }
    }

    void OrderbookManager::handle_orderbook_update(const std::string &asset_id, const Orderbook &book)
    {
        // Store orderbook
        {
            std::unique_lock<std::shared_mutex> lock(orderbooks_mutex_);
            orderbooks_[asset_id] = book;
        }

        total_updates_++;

        // Find the condition this token belongs to
        auto cond_it = token_to_condition_.find(asset_id);
        if (cond_it == token_to_condition_.end())
        {
            return;
        }

        const std::string &condition_id = cond_it->second;

        // Update market state
        {
            std::unique_lock<std::shared_mutex> lock(markets_mutex_);
            auto market_it = markets_.find(condition_id);
            if (market_it != markets_.end())
            {
                auto &market = *market_it->second;

                if (asset_id == market.token_yes)
                {
                    market.best_ask_yes.store(book.best_ask(), std::memory_order_relaxed);
                    market.best_ask_yes_size.store(book.best_ask_size(), std::memory_order_relaxed);
                }
                else if (asset_id == market.token_no)
                {
                    market.best_ask_no.store(book.best_ask(), std::memory_order_relaxed);
                    market.best_ask_no_size.store(book.best_ask_size(), std::memory_order_relaxed);
                }

                market.last_update_ns.store(book.timestamp_ns, std::memory_order_relaxed);
                market.update_count.fetch_add(1, std::memory_order_relaxed);
            }
        }

        // Callback
        if (on_update_cb_)
        {
            on_update_cb_(asset_id, book);
        }

        // Check for arb opportunity
        check_arb_opportunity(condition_id);
    }

    void OrderbookManager::check_arb_opportunity(const std::string &condition_id)
    {
        std::shared_lock<std::shared_mutex> lock(markets_mutex_);
        auto it = markets_.find(condition_id);
        if (it == markets_.end())
        {
            return;
        }

        const auto &market = *it->second;
        double combined = market.combined();

        // Check if both prices are set
        double ask_yes = market.best_ask_yes.load(std::memory_order_relaxed);
        double ask_no = market.best_ask_no.load(std::memory_order_relaxed);

        if (ask_yes <= 0 || ask_no <= 0)
        {
            return;
        }

        if (combined < config_.trigger_combined)
        {
            arb_opportunities_++;

            if (on_arb_cb_)
            {
                on_arb_cb_(market, combined);
            }
        }
    }

} // namespace polymarket
