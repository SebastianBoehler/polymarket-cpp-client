#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <atomic>
#include <chrono>

namespace polymarket
{

    // Price level in orderbook
    struct PriceLevel
    {
        double price{0.0};
        double size{0.0};
    };

    // Orderbook for a single token
    struct Orderbook
    {
        std::string asset_id;
        std::vector<PriceLevel> bids;
        std::vector<PriceLevel> asks;
        uint64_t timestamp_ns{0};

        // Best bid = highest bid price
        double best_bid() const
        {
            if (bids.empty())
                return 0.0;
            double max_bid = bids[0].price;
            for (const auto &b : bids)
            {
                if (b.price > max_bid)
                    max_bid = b.price;
            }
            return max_bid;
        }

        // Best ask = lowest ask price (API returns asks in descending order)
        double best_ask() const
        {
            if (asks.empty())
                return 1.0;
            double min_ask = asks[0].price;
            for (const auto &a : asks)
            {
                if (a.price < min_ask)
                    min_ask = a.price;
            }
            return min_ask;
        }

        double best_bid_size() const
        {
            if (bids.empty())
                return 0.0;
            double max_bid = bids[0].price;
            double size = bids[0].size;
            for (const auto &b : bids)
            {
                if (b.price > max_bid)
                {
                    max_bid = b.price;
                    size = b.size;
                }
            }
            return size;
        }

        double best_ask_size() const
        {
            if (asks.empty())
                return 0.0;
            double min_ask = asks[0].price;
            double size = asks[0].size;
            for (const auto &a : asks)
            {
                if (a.price < min_ask)
                {
                    min_ask = a.price;
                    size = a.size;
                }
            }
            return size;
        }
    };

    // Token info
    struct Token
    {
        std::string token_id;
        std::string outcome; // "Yes" or "No"
    };

    // Market from CLOB API
    struct ClobMarket
    {
        std::string condition_id;
        std::string question;
        std::string market_slug;
        std::vector<Token> tokens;
        bool neg_risk{false};
        bool active{false};
        bool closed{false};
        double minimum_order_size{0.0};
        std::string minimum_tick_size;
        uint64_t end_time_ms{0};
        bool fees_enabled{false};
        double maker_base_fee{0.0};
        double taker_base_fee{0.0};
        double fee_rate{0.0};
        int fee_exponent{1};

        std::string token_yes() const
        {
            for (const auto &t : tokens)
            {
                if (t.outcome == "Yes")
                    return t.token_id;
            }
            return "";
        }

        std::string token_no() const
        {
            for (const auto &t : tokens)
            {
                if (t.outcome == "No")
                    return t.token_id;
            }
            return "";
        }
    };

    struct ClobMarketPage
    {
        std::vector<ClobMarket> data;
        std::string next_cursor;
    };

    // Market state for arbitrage tracking (copyable version for fetching)
    struct MarketState
    {
        std::string slug;
        std::string title;
        std::string symbol;
        std::string condition_id;
        std::string token_yes;
        std::string token_no;

        // Orderbook state (non-atomic for copyability during fetch)
        double best_ask_yes{0.0};
        double best_ask_no{0.0};
        double best_ask_yes_size{0.0};
        double best_ask_no_size{0.0};

        double minimum_order_size{0.0};
        std::string minimum_tick_size;
        uint64_t end_time_ms{0};
        bool neg_risk{false};
        bool fees_enabled{false};
        double fee_rate{0.0};
        int fee_exponent{1};

        // Tracking
        // Compatibility aggregate: latest timestamp from either leg.
        uint64_t last_update_ns{0};
        uint64_t last_update_yes_ns{0};
        uint64_t last_update_no_ns{0};
        uint32_t update_count{0};

        double combined() const
        {
            return best_ask_yes + best_ask_no;
        }

        bool is_arb_opportunity(double threshold = 0.98) const
        {
            return combined() < threshold;
        }
    };

    // Thread-safe market state for live orderbook tracking
    struct LiveMarketState
    {
        std::string slug;
        std::string title;
        std::string symbol;
        std::string condition_id;
        std::string token_yes;
        std::string token_no;

        // Orderbook state (atomic for thread safety)
        std::atomic<double> best_ask_yes{0.0};
        std::atomic<double> best_ask_no{0.0};
        std::atomic<double> best_ask_yes_size{0.0};
        std::atomic<double> best_ask_no_size{0.0};

        double minimum_order_size{0.0};
        std::string minimum_tick_size;
        uint64_t end_time_ms{0};
        bool neg_risk{false};
        bool fees_enabled{false};
        double fee_rate{0.0};
        int fee_exponent{1};

        // Tracking
        // Compatibility aggregate: latest timestamp from either leg.
        std::atomic<uint64_t> last_update_ns{0};
        std::atomic<uint64_t> last_update_yes_ns{0};
        std::atomic<uint64_t> last_update_no_ns{0};
        std::atomic<uint32_t> update_count{0};

        // Constructor from MarketState
        LiveMarketState() = default;

        explicit LiveMarketState(const MarketState &m)
            : slug(m.slug), title(m.title), symbol(m.symbol), condition_id(m.condition_id), token_yes(m.token_yes), token_no(m.token_no),
              minimum_order_size(m.minimum_order_size), minimum_tick_size(m.minimum_tick_size), end_time_ms(m.end_time_ms),
              neg_risk(m.neg_risk), fees_enabled(m.fees_enabled), fee_rate(m.fee_rate), fee_exponent(m.fee_exponent)
        {
            best_ask_yes.store(m.best_ask_yes);
            best_ask_no.store(m.best_ask_no);
            best_ask_yes_size.store(m.best_ask_yes_size);
            best_ask_no_size.store(m.best_ask_no_size);
            uint64_t latest = m.last_update_ns;
            if (m.last_update_yes_ns > latest) latest = m.last_update_yes_ns;
            if (m.last_update_no_ns > latest) latest = m.last_update_no_ns;
            last_update_ns.store(latest);
            last_update_yes_ns.store(m.last_update_yes_ns);
            last_update_no_ns.store(m.last_update_no_ns);
        }

        double combined() const
        {
            return best_ask_yes.load(std::memory_order_relaxed) +
                   best_ask_no.load(std::memory_order_relaxed);
        }

        bool is_arb_opportunity(double threshold = 0.98) const
        {
            return combined() < threshold;
        }
    };

    // WebSocket message types
    enum class WsMessageType
    {
        ORDERBOOK_SNAPSHOT,
        ORDERBOOK_UPDATE,
        TRADE,
        UNKNOWN
    };

    // Configuration
    struct Config
    {
        // API endpoints
        std::string clob_rest_url = "https://clob.polymarket.com";
        std::string clob_ws_url = "wss://ws-subscriptions-clob.polymarket.com/ws/market";
        std::string gamma_api_url = "https://gamma-api.polymarket.com";
        std::string rtds_ws_url = "wss://ws-live-data.polymarket.com";

        // Trading parameters
        double trigger_combined = 0.98;

        // Connection settings
        int ws_ping_interval_ms = 10000;
        int ws_connect_timeout_ms = 5000;
        uint64_t max_book_age_ms = 2000;
        int http_timeout_ms = 5000;
        int max_markets = 50;

        // Crypto tickers for 15m/4h/1h markets
        std::vector<std::string> crypto_tickers = {
            "btc", "eth", "xrp", "sol", "doge", "bnb",
            "ada", "avax", "matic", "link", "dot", "ltc"};
    };

    // Monotonic nanoseconds for receipt ages and latency measurements.
    inline uint64_t now_ns()
    {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    // Utility: get current time in seconds (Unix timestamp)
    inline uint64_t now_sec()
    {
        return std::chrono::duration_cast<std::chrono::seconds>(
                   std::chrono::system_clock::now().time_since_epoch())
            .count();
    }

} // namespace polymarket
