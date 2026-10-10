#pragma once

#include "polymarket/clob_types.hpp"
#include "polymarket/environment.hpp"
#include "polymarket/geoblock.hpp"
#include "polymarket/market_price.hpp"
#include "polymarket/types.hpp"
#include "polymarket/http_client.hpp"
#include "polymarket/order_signer.hpp"
#include "polymarket/sdk_error.hpp"
#include <string>
#include <vector>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <optional>
#include <set>
#include <map>
#include <memory>
#include <mutex>

namespace polymarket
{
    class TradeStatusTracker;

    // Comprehensive CLOB client for Polymarket
    class ClobClient
    {
    public:
        ClobClient(const std::string &base_url = "https://clob.polymarket.com", int chain_id = 137);
        ClobClient(const std::string &base_url, int chain_id, const HttpClientOptions &http_options);
        ClobClient(const std::string &base_url, int chain_id, const std::string &private_key,
                   SignatureType sig_type = SignatureType::EOA,
                   const std::string &funder_address = "");
        ClobClient(const std::string &base_url, int chain_id, const std::string &private_key,
                   SignatureType sig_type, const std::string &funder_address,
                   const HttpClientOptions &http_options);
        ClobClient(const std::string &base_url, int chain_id,
                   const std::string &private_key,
                   const ApiCredentials &creds,
                   SignatureType sig_type = SignatureType::EOA,
                   const std::string &funder_address = "");
        ClobClient(const std::string &base_url, int chain_id,
                   const std::string &private_key,
                   const ApiCredentials &creds,
                   SignatureType sig_type,
                   const std::string &funder_address,
                   const HttpClientOptions &http_options);

        // The constructors above change only the production CLOB host (chain 137 only).
        // These take every host and contract from a validated `environment`.
        explicit ClobClient(const Environment &environment,
                            const std::optional<HttpClientOptions> &http_options = std::nullopt);
        ClobClient(const Environment &environment, const std::string &private_key,
                   SignatureType sig_type = SignatureType::EOA,
                   const std::string &funder_address = "",
                   const std::optional<HttpClientOptions> &http_options = std::nullopt);
        ClobClient(const Environment &environment, const std::string &private_key,
                   const ApiCredentials &creds, SignatureType sig_type = SignatureType::EOA,
                   const std::string &funder_address = "",
                   const std::optional<HttpClientOptions> &http_options = std::nullopt);

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
        ClobMarketPage get_markets(const std::string &next_cursor = "");
        std::optional<ClobMarket> get_market(const std::string &condition_id);
        ClobMarketPage get_sampling_markets(const std::string &next_cursor = "");
        ClobMarketPage get_simplified_markets(const std::string &next_cursor = "");
        ClobMarketPage get_sampling_simplified_markets(const std::string &next_cursor = "");

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
        void clear_market_metadata_cache(const std::string &token_id = "");

        // Fetches the book and the market tick size, then simulates a market
        // order of `amount` (collateral for BUY, shares for SELL). The estimated
        // price is the one create_market_order would sign against the same book.
        Result<MarketPriceEstimate> estimate_market_price(const std::string &token_id,
                                                          OrderSide side, double amount,
                                                          OrderType order_type = OrderType::FOK);

        // Prices history
        using PriceHistoryPoint = ::polymarket::PriceHistoryPoint;
        std::vector<PriceHistoryPoint> get_prices_history(const std::string &token_id,
                                                          uint64_t start_ts = 0,
                                                          uint64_t end_ts = 0,
                                                          const std::string &interval = "1h",
                                                          const std::string &fidelity = "1");

        using LiveActivityMarket = ::polymarket::LiveActivityMarket;
        std::optional<LiveActivityMarket> get_market_live_activity(const std::string &condition_id);

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
        PreparedOrder create_market_order(const CreateMarketOrderParams &params);
        PreparedOrder create_market_order(const CreateMarketOrderParams &params, OrderType order_type);
        Result<SignedOrder> create_order_result(const CreateOrderParams &params);
        Result<PreparedOrder> create_market_order_result(const CreateMarketOrderParams &params);
        Result<PreparedOrder> create_market_order_result(const CreateMarketOrderParams &params,
                                                         OrderType order_type);

        OrderResponse post_order(const PreparedOrder &prepared);
        Result<OrderResponse> post_order_result(const PreparedOrder &prepared);
        OrderResponse post_order(const SignedOrder &order, OrderType order_type);
        Result<OrderResponse> post_order_result(const SignedOrder &order, OrderType order_type);
        std::vector<OrderResponse> post_orders(const std::vector<BatchOrderEntry> &orders);

        // Combined create and post
        OrderResponse create_and_post_order(const CreateOrderParams &params,
                                            OrderType order_type = OrderType::GTC);
        OrderResponse create_and_post_market_order(const CreateMarketOrderParams &params,
                                                   OrderType order_type = OrderType::FAK);

        // Validates, signs and posts a limit order: GTC, or GTD when an
        // expiration is set. When the tick size comes from the market, a price
        // off the cached grid refreshes it once, since ticks change near 0 and 1.
        // Nothing is posted if validation or signing fails.
        Result<OrderResponse> place_limit_order(const PlaceLimitOrderParams &params);

        // Validates, signs and posts a FAK or FOK market order. Without a
        // worst_price it walks the book like estimate_market_price, so a FOK
        // the book cannot fill fails with InsufficientLiquidity before signing.
        // The market tick size is refreshed once if it rejects the price.
        Result<OrderResponse> place_market_order(const PlaceMarketOrderParams &params);

        // Order management
        bool cancel_order(const std::string &order_id);
        Result<bool> cancel_order_result(const std::string &order_id);
        bool cancel_orders(const std::vector<std::string> &order_ids);
        bool cancel_all();
        bool cancel_market_orders(const std::string &condition_id);

        // Order queries
        std::optional<OpenOrder> get_order(const std::string &order_id);
        Result<std::optional<OpenOrder>> get_order_result(const std::string &order_id);
        std::vector<OpenOrder> get_open_orders(const std::string &market = "");
        Result<std::vector<OpenOrder>> get_open_orders_result(const std::string &market = "");
        std::vector<Trade> get_trades(const std::string &next_cursor = "");
        // Empty when the account has no trade with this ID (yet).
        std::optional<Trade> get_trade(const std::string &trade_id);
        Result<std::optional<Trade>> get_trade_result(const std::string &trade_id);

        // Blocks until every fill in `order.trade_ids` is CONFIRMED or FAILED,
        // polling get_trade_result. Confirmed fills must have a final hash.
        // Covers only the fills matched when the order was posted, not later
        // fills of a remainder resting on the book.
        // Fails with Timeout while fills are still settling (the order itself
        // is unaffected), TransactionFailed when every fill failed, or the
        // error of the first trade lookup that fails.
        Result<OrderSettlement> wait_for_order_fill_settlement(
            const OrderResponse &order,
            std::chrono::milliseconds timeout = std::chrono::seconds(30),
            std::chrono::milliseconds poll_interval = std::chrono::milliseconds(250));

        // Same result, driven by fills a UserStream reports through `tracker`:
        // returns as soon as the stream shows every fill settled. Trades are
        // looked up through REST when a confirmation lacks its final hash,
        // after the stream recovers from a gap, every `reconcile_interval` as
        // a safety net, and once at the deadline.
        Result<OrderSettlement> wait_for_order_fill_settlement(
            const OrderResponse &order, const TradeStatusTracker &tracker,
            std::chrono::milliseconds timeout = std::chrono::seconds(30),
            std::chrono::milliseconds reconcile_interval = std::chrono::seconds(5));

        // Balance and allowance
        std::optional<BalanceAllowance> get_balance_allowance(
            const std::string &asset_type = "COLLATERAL",
            const std::string &token_id = "");
        bool update_balance_allowance(const std::string &asset_type = "COLLATERAL",
                                      const std::string &token_id = "");

        // Order scoring
        std::optional<OrderScoringResult> is_order_scoring(const std::string &order_id);
        std::optional<OrderScoringResult> is_order_scoring(const SignedOrder &order);
        std::vector<OrderScoringResult> are_orders_scoring(const std::vector<std::string> &order_ids);
        std::vector<OrderScoringResult> are_orders_scoring(const std::vector<SignedOrder> &orders);

        // Notifications
        using Notification = ::polymarket::Notification;
        std::vector<Notification> get_notifications();
        bool drop_notifications(const std::vector<std::string> &notification_ids);

        // Rewards (market maker incentives)
        using RewardsInfo = ::polymarket::RewardsInfo;
        std::vector<RewardsInfo> get_rewards_markets_current();
        std::vector<RewardsInfo> get_rewards_markets(const std::string &condition_id);

        using EarningsInfo = ::polymarket::EarningsInfo;
        std::optional<EarningsInfo> get_earnings_for_user_for_day(const std::string &date = "");
        std::vector<EarningsInfo> get_earnings_for_user_for_day_all(const std::string &date);
        std::optional<EarningsInfo> get_total_earnings_for_user_for_day(const std::string &date = "");
        std::vector<EarningsInfo> get_total_earnings_for_user_for_day_all(const std::string &date);

        // Fee rate
        using FeeRateInfo = ::polymarket::FeeRateInfo;
        std::optional<FeeRateInfo> get_fee_rate(const std::string &token_id);

        // ============================================================
        // UTILITY METHODS
        // ============================================================

        // Check if client is authenticated
        bool is_authenticated() const { return order_signer_ != nullptr && api_creds_ != nullptr; }

        // Get signer address
        std::string get_address() const;

        // Get funder address (for proxy wallets)
        std::string get_funder_address() const { return funder_address_; }

        // Set timeout
        void set_timeout_ms(long timeout_ms)
        {
            http_.set_timeout_ms(timeout_ms);
            order_http_.set_timeout_ms(timeout_ms);
            data_http_.set_timeout_ms(timeout_ms);
        }

        // Apply transport options in one call
        void configure_transport(const HttpClientOptions &options)
        {
            http_.configure(options);
            order_http_.configure(options);
            data_http_.configure(options);
        }

        // Set proxy for HTTP requests (e.g., "http://user:pass@proxy.example.com:8080")
        void set_proxy(const std::string &proxy_url)
        {
            http_.set_proxy(proxy_url);
            order_http_.set_proxy(proxy_url);
            data_http_.set_proxy(proxy_url);
        }

        // Order-placement eligibility for this client's route (proxy/interface included)
        Result<GeoblockStatus> get_geoblock_status() const
        {
            return check_geoblock(http_.options());
        }

        // Set custom user agent
        void set_user_agent(const std::string &user_agent)
        {
            http_.set_user_agent(user_agent);
            order_http_.set_user_agent(user_agent);
            data_http_.set_user_agent(user_agent);
        }

        // DNS cache timeout (default: 60s)
        void set_dns_cache_timeout(long seconds)
        {
            http_.set_dns_cache_timeout(seconds);
            order_http_.set_dns_cache_timeout(seconds);
            data_http_.set_dns_cache_timeout(seconds);
        }

        // TCP keepalive probe interval
        void set_keepalive_interval(long seconds)
        {
            http_.set_keepalive_interval(seconds);
            order_http_.set_keepalive_interval(seconds);
            data_http_.set_keepalive_interval(seconds);
        }

        // Receives Poly-RateLimit-* state reported by order and cancel responses,
        // on the thread that made the request. Listener exceptions are ignored;
        // an empty function removes the listener.
        void set_rate_limit_listener(std::function<void(const RateLimitUpdate &)> listener);

        // Read requests that get HTTP 429 are retried after the server's
        // Retry-After delay; orders, cancellations and other writes never are.
        // Defaults to RateLimitRetry{}; std::nullopt disables retries.
        void set_rate_limit_retry(std::optional<RateLimitRetry> policy);

        // ============================================================
        // CONNECTION WARMING (for low-latency trading)
        // ============================================================

        // Orders and cancellations use their own connection, so they never wait
        // behind reads on the shared one. Both share the same transport options.

        // Pre-warm TCP/TLS on both connections to reduce first-request latency
        // Call this after startup to establish connections before trading
        bool warm_connection();

        // Start background heartbeat on both connections (default: 25s interval)
        // This prevents the server from closing the keep-alive connections
        void start_heartbeat(long interval_seconds = 25)
        {
            http_.start_heartbeat(interval_seconds);
            order_http_.start_heartbeat(interval_seconds);
        }

        // Stop background heartbeat
        void stop_heartbeat()
        {
            http_.stop_heartbeat();
            order_http_.stop_heartbeat();
        }

        // Check if heartbeat is running
        bool is_heartbeat_running() const { return http_.is_heartbeat_running(); }

        // Read and market-data connection statistics
        HttpClient::ConnectionStats get_connection_stats() const { return http_.get_stats(); }
        RequestMetrics get_last_request_metrics() const { return http_.get_last_request_metrics(); }

        // Order and cancellation connection statistics
        HttpClient::ConnectionStats get_order_connection_stats() const
        {
            return order_http_.get_stats();
        }
        RequestMetrics get_last_order_request_metrics() const
        {
            return order_http_.get_last_request_metrics();
        }

        const Environment &environment() const { return environment_; }

        std::string get_exchange_address() const;
        std::string get_neg_risk_exchange_address() const;

        // ============================================================
        // POSITION MANAGEMENT (Data API)
        // ============================================================

        // Position info from Data API
        using Position = ::polymarket::Position;

        // Get user positions from Data API
        std::vector<Position> get_positions(const std::string &user_address = "");

        // Get positions that can be redeemed (market resolved, user holds winning outcome)
        std::vector<Position> get_redeemable_positions(const std::string &user_address = "");

        // Get positions that can be merged (user holds both Yes and No outcomes)
        std::vector<Position> get_mergeable_positions(const std::string &user_address = "");

    private:
        HttpClient http_;
        HttpClient order_http_;
        HttpClient data_http_;
        Environment environment_;
        std::string funder_address_;
        SignatureType sig_type_;

        // Order signer (null for public access)
        std::unique_ptr<OrderSigner> order_signer_;
        std::unique_ptr<ApiCredentials> api_creds_;

        // Every public constructor delegates here; null private_key means public access.
        ClobClient(Environment environment, const std::string *private_key,
                   const ApiCredentials *creds, SignatureType sig_type,
                   const std::string &funder_address,
                   const std::optional<HttpClientOptions> &http_options);

        template <typename Value>
        struct MetadataCacheEntry
        {
            Value value;
            std::chrono::steady_clock::time_point expires_at;
        };

        mutable std::mutex metadata_cache_mutex_;
        std::condition_variable metadata_cache_cv_;
        std::set<std::string> metadata_cache_in_flight_;
        std::map<std::string, MetadataCacheEntry<TickSizeInfo>> tick_size_cache_;
        std::map<std::string, MetadataCacheEntry<NegRiskInfo>> neg_risk_cache_;
        Result<TickSizeInfo> tick_size_result(const std::string &token_id);
        std::optional<TickSizeInfo> cached_tick_size(const std::string &token_id) const;
        void evict_tick_size(const std::string &token_id);
        void remember_market_metadata(const std::string &token_id,
                                      const std::optional<std::string> &tick_size,
                                      std::optional<bool> neg_risk);
        Result<NegRiskInfo> neg_risk_result(const std::string &token_id);
        Result<Orderbook> order_book_result(const std::string &token_id);

        using RateLimitListener = std::function<void(const RateLimitUpdate &)>;
        mutable std::mutex rate_limit_mutex_;
        std::shared_ptr<const RateLimitListener> rate_limit_listener_;
        std::optional<RateLimitRetry> rate_limit_retry_{RateLimitRetry{}};
        void notify_rate_limit(const HttpResponse &response, RateLimitUpdate::Bucket bucket) const;
        std::optional<RateLimitRetry> rate_limit_retry() const;
        // Runs a read request under the retry policy; attempt() must rebuild
        // signed headers. Defined in clob_client_internal.hpp.
        template <typename Attempt>
        HttpResponse
        read(Attempt &&attempt,
             std::optional<std::chrono::steady_clock::time_point> deadline = std::nullopt) const;
        Result<std::optional<Trade>>
        lookup_trade(const std::string &trade_id,
                     std::optional<std::chrono::steady_clock::time_point> deadline);

        // Helper methods
        std::map<std::string, std::string> get_l2_headers(const std::string &method,
                                                          const std::string &path,
                                                          const std::string &body = "");

        std::string order_type_to_string(OrderType type);
        std::string order_side_to_string(OrderSide side);
        Result<OrderResponse> post_signed_order(const SignedOrder &order, OrderType order_type,
                                                bool post_only);

        // JSON parsing helpers
        std::vector<ClobMarket> parse_markets(const std::string &json);
        std::optional<Orderbook> parse_orderbook(
            const std::string &json, const std::string &expected_asset_id = {});
        OrderResponse parse_order_response(const std::string &json);
        std::vector<OpenOrder> parse_open_orders(const std::string &json);
    };

} // namespace polymarket
