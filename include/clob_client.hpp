#pragma once

#include "types.hpp"
#include "http_client.hpp"
#include "order_signer.hpp"
#include <string>
#include <vector>
#include <optional>
#include <map>
#include <memory>

namespace polymarket
{

    // Order types supported by Polymarket
    enum class OrderType
    {
        GTC, // Good-Til-Cancelled
        GTD, // Good-Til-Date
        FOK, // Fill-Or-Kill
        FAK  // Fill-And-Kill (IOC)
    };

    // Order response from API
    struct OrderResponse
    {
        bool success;
        std::string error_msg;
        std::string order_id;
        std::vector<std::string> transaction_hashes;
        std::string status;
        std::string taking_amount; // Shares received
        std::string making_amount; // USDC spent
    };

    // Open order info
    struct OpenOrder
    {
        std::string id;
        std::string market;
        std::string asset_id;
        std::string side;
        std::string original_size;
        std::string size_matched;
        std::string price;
        std::string status;
        std::string created_at;
        std::string expiration;
        std::string order_type;
    };

    // Trade info
    struct Trade
    {
        std::string id;
        std::string market;
        std::string asset_id;
        std::string side;
        std::string size;
        std::string price;
        std::string fee_rate_bps;
        std::string status;
        std::string created_at;
        std::string match_time;
        std::string transaction_hash;
    };

    // Balance/Allowance info
    struct BalanceAllowance
    {
        std::string balance;
        std::string allowance;
    };

    // Price info
    struct PriceInfo
    {
        std::string token_id;
        double price;
    };

    // Midpoint info
    struct MidpointInfo
    {
        std::string token_id;
        double mid;
    };

    // Spread info
    struct SpreadInfo
    {
        std::string token_id;
        double spread;
    };

    // Tick size info
    struct TickSizeInfo
    {
        std::string minimum_tick_size;
    };

    // Neg risk info
    struct NegRiskInfo
    {
        bool neg_risk;
    };

    // Order scoring result
    struct OrderScoringResult
    {
        bool scoring;
    };

    // Create order parameters
    struct CreateOrderParams
    {
        std::string token_id;
        double price;
        double size;
        OrderSide side;
        std::string fee_rate_bps = "0";
        std::string expiration = "0";
        std::string nonce = "0";
    };

    // Create market order parameters
    struct CreateMarketOrderParams
    {
        std::string token_id;
        double amount; // USDC for BUY, shares for SELL
        OrderSide side;
        std::optional<double> price; // Optional price limit
    };

    // Batch order entry
    struct BatchOrderEntry
    {
        SignedOrder order;
        OrderType order_type;
    };

    // Comprehensive CLOB client for Polymarket
    class ClobClient
    {
    public:
        // Constructor for public (unauthenticated) access
        ClobClient(const std::string &base_url = "https://clob.polymarket.com", int chain_id = 137);

        // Constructor for authenticated access
        ClobClient(const std::string &base_url, int chain_id,
                   const std::string &private_key,
                   const ApiCredentials &creds,
                   SignatureType sig_type = SignatureType::EOA,
                   const std::string &funder_address = "");

        ~ClobClient();

        // Disable copy
        ClobClient(const ClobClient &) = delete;
        ClobClient &operator=(const ClobClient &) = delete;

        // ============================================================
        // PUBLIC ENDPOINTS (No authentication required)
        // ============================================================

        // Server time
        std::optional<uint64_t> get_server_time();

        // Markets
        std::vector<ClobMarket> get_markets(const std::string &next_cursor = "");
        std::optional<ClobMarket> get_market(const std::string &condition_id);
        std::vector<ClobMarket> get_sampling_markets(const std::string &next_cursor = "");
        std::vector<ClobMarket> get_simplified_markets(const std::string &next_cursor = "");
        std::vector<ClobMarket> get_sampling_simplified_markets(const std::string &next_cursor = "");

        // Orderbook
        std::optional<Orderbook> get_order_book(const std::string &token_id);
        std::map<std::string, Orderbook> get_order_books(const std::vector<std::string> &token_ids);

        // Prices
        std::optional<PriceInfo> get_price(const std::string &token_id, const std::string &side = "buy");
        std::vector<PriceInfo> get_prices(const std::vector<std::string> &token_ids, const std::string &side = "buy");
        std::optional<PriceInfo> get_last_trade_price(const std::string &token_id);
        std::vector<PriceInfo> get_last_trades_prices(const std::vector<std::string> &token_ids);

        // Midpoints
        std::optional<MidpointInfo> get_midpoint(const std::string &token_id);
        std::vector<MidpointInfo> get_midpoints(const std::vector<std::string> &token_ids);

        // Spreads
        std::optional<SpreadInfo> get_spread(const std::string &token_id);
        std::vector<SpreadInfo> get_spreads(const std::vector<std::string> &token_ids);

        // Market info
        std::optional<TickSizeInfo> get_tick_size(const std::string &token_id);
        std::optional<NegRiskInfo> get_neg_risk(const std::string &token_id);

        // Prices history
        struct PriceHistoryPoint
        {
            uint64_t timestamp;
            double price;
        };
        std::vector<PriceHistoryPoint> get_prices_history(const std::string &token_id,
                                                          uint64_t start_ts = 0,
                                                          uint64_t end_ts = 0,
                                                          const std::string &interval = "1h",
                                                          const std::string &fidelity = "1");

        // Market trades/events
        std::vector<Trade> get_market_trades_events(const std::string &condition_id,
                                                    const std::string &next_cursor = "");

        // ============================================================
        // AUTHENTICATED ENDPOINTS (L1 - API Key management)
        // ============================================================

        // API Key management
        ApiCredentials create_api_key(uint64_t nonce = 0);
        ApiCredentials derive_api_key();
        ApiCredentials create_or_derive_api_key();
        std::vector<std::string> get_api_keys();
        bool delete_api_key();

        // ============================================================
        // AUTHENTICATED ENDPOINTS (L2 - Trading)
        // ============================================================

        // Order creation (creates signed order, does not post)
        SignedOrder create_order(const CreateOrderParams &params);
        // Overload with cached neg_risk to avoid HTTP call
        SignedOrder create_order(const CreateOrderParams &params, bool is_neg_risk);
        SignedOrder create_market_order(const CreateMarketOrderParams &params);

        // Order posting
        OrderResponse post_order(const SignedOrder &order, OrderType order_type = OrderType::GTC);
        std::vector<OrderResponse> post_orders(const std::vector<BatchOrderEntry> &orders);

        // Combined create and post
        OrderResponse create_and_post_order(const CreateOrderParams &params,
                                            OrderType order_type = OrderType::GTC);
        OrderResponse create_and_post_market_order(const CreateMarketOrderParams &params,
                                                   OrderType order_type = OrderType::FAK);

        // Order management
        bool cancel_order(const std::string &order_id);
        bool cancel_orders(const std::vector<std::string> &order_ids);
        bool cancel_all();
        bool cancel_market_orders(const std::string &condition_id);

        // Order queries
        std::optional<OpenOrder> get_order(const std::string &order_id);
        std::vector<OpenOrder> get_open_orders(const std::string &market = "");
        std::vector<Trade> get_trades(const std::string &next_cursor = "");

        // Balance and allowance
        std::optional<BalanceAllowance> get_balance_allowance(const std::string &asset_type = "USDC");
        bool update_balance_allowance(const std::string &asset_type = "USDC");

        // Order scoring
        std::optional<OrderScoringResult> is_order_scoring(const SignedOrder &order);
        std::vector<OrderScoringResult> are_orders_scoring(const std::vector<SignedOrder> &orders);

        // Notifications
        struct Notification
        {
            std::string id;
            std::string type;
            std::string message;
            std::string created_at;
        };
        std::vector<Notification> get_notifications();
        bool drop_notifications(const std::vector<std::string> &notification_ids);

        // Rewards (market maker incentives)
        struct RewardsInfo
        {
            std::string market;
            std::string min_size;
            std::string max_spread;
            std::string reward_epoch;
        };
        std::vector<RewardsInfo> get_rewards_markets_current();
        std::vector<RewardsInfo> get_rewards_markets(const std::string &epoch = "");

        struct EarningsInfo
        {
            std::string market;
            std::string earnings;
            std::string epoch;
        };
        std::optional<EarningsInfo> get_earnings_for_user_for_day(const std::string &date = "");
        std::optional<EarningsInfo> get_total_earnings_for_user_for_day(const std::string &date = "");

        // Fee rate
        struct FeeRateInfo
        {
            std::string maker;
            std::string taker;
        };
        std::optional<FeeRateInfo> get_fee_rate();

        // ============================================================
        // UTILITY METHODS
        // ============================================================

        // Check if client is authenticated
        bool is_authenticated() const { return order_signer_ != nullptr; }

        // Get signer address
        std::string get_address() const;

        // Get funder address (for proxy wallets)
        std::string get_funder_address() const { return funder_address_; }

        // Set timeout
        void set_timeout_ms(long timeout_ms) { http_.set_timeout_ms(timeout_ms); }

        // Set proxy for HTTP requests (e.g., "http://user:pass@proxy.example.com:8080")
        void set_proxy(const std::string &proxy_url) { http_.set_proxy(proxy_url); }

        // Set custom user agent
        void set_user_agent(const std::string &user_agent) { http_.set_user_agent(user_agent); }

        // Get exchange address for the chain
        std::string get_exchange_address() const;
        std::string get_neg_risk_exchange_address() const;

    private:
        HttpClient http_;
        int chain_id_;
        std::string base_url_;
        std::string funder_address_;
        SignatureType sig_type_;

        // Order signer (null for public access)
        std::unique_ptr<OrderSigner> order_signer_;
        std::unique_ptr<ApiCredentials> api_creds_;

        // Helper methods
        std::map<std::string, std::string> get_l2_headers(const std::string &method,
                                                          const std::string &path,
                                                          const std::string &body = "");

        std::string order_type_to_string(OrderType type);
        std::string order_side_to_string(OrderSide side);

        // JSON parsing helpers
        std::vector<ClobMarket> parse_markets(const std::string &json);
        std::optional<Orderbook> parse_orderbook(const std::string &json);
        OrderResponse parse_order_response(const std::string &json);
        std::vector<OpenOrder> parse_open_orders(const std::string &json);
        std::vector<Trade> parse_trades(const std::string &json);
    };

} // namespace polymarket
