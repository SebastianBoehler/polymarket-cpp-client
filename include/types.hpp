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
        double price;
        double size;
    };

    // Orderbook for a single token
    struct Orderbook
    {
        std::string asset_id;
        std::vector<PriceLevel> bids;
        std::vector<PriceLevel> asks;
        uint64_t timestamp_ns;

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
        bool neg_risk;
        bool active;
        bool closed;

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

        // Tracking
        uint64_t last_update_ns{0};
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

        // Tracking
        std::atomic<uint64_t> last_update_ns{0};
        std::atomic<uint32_t> update_count{0};

        // Constructor from MarketState
        LiveMarketState() = default;

        explicit LiveMarketState(const MarketState &m)
            : slug(m.slug), title(m.title), symbol(m.symbol), condition_id(m.condition_id), token_yes(m.token_yes), token_no(m.token_no)
        {
            best_ask_yes.store(m.best_ask_yes);
            best_ask_no.store(m.best_ask_no);
            best_ask_yes_size.store(m.best_ask_yes_size);
            best_ask_no_size.store(m.best_ask_no_size);
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
        double max_combined = 0.99;
        double size_usdc = 5.0;

        // Connection settings
        int ws_ping_interval_ms = 5000;
        int http_timeout_ms = 5000;
        int max_markets = 50;

        // Crypto tickers for 15m/4h/1h markets
        std::vector<std::string> crypto_tickers = {
            "btc", "eth", "xrp", "sol", "doge", "bnb",
            "ada", "avax", "matic", "link", "dot", "ltc"};
    };

    // Utility: get current time in nanoseconds
    inline uint64_t now_ns()
    {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
                   std::chrono::high_resolution_clock::now().time_since_epoch())
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
