#pragma once

#include "types.hpp"
#include "http_client.hpp"
#include <vector>
#include <optional>
#include <utility>

namespace polymarket
{
    namespace detail
    {
        std::vector<ClobMarket> parse_clob_markets_json(const std::string &json);
        ClobMarketPage parse_clob_market_page_json(const std::string &json);
        std::vector<ClobMarket> filter_neg_risk_markets(const std::vector<ClobMarket> &markets,
                                                        std::size_t limit);
        std::optional<MarketState> parse_gamma_market_json(const std::string &json,
                                                           const std::string &ticker);
        std::string format_new_york_hour_slug(const std::string &asset_name,
                                              uint64_t unix_seconds);
        std::string ticker_from_hour_slug(const std::string &slug);
        std::vector<uint64_t> aligned_market_timestamps(uint64_t now,
                                                        uint64_t interval,
                                                        int count);
        bool apply_clob_market_info_json(MarketState &market, const std::string &json);
    }

    // Market fetcher for Polymarket REST APIs
    class MarketFetcher
    {
    public:
        explicit MarketFetcher(const Config &config);
        ~MarketFetcher() = default;

        // Fetch markets from CLOB API
        std::vector<ClobMarket> fetch_all_markets(int max_markets = 100);
        std::vector<ClobMarket> fetch_neg_risk_markets(int max_markets = 50);
        std::optional<ClobMarket> fetch_market(const std::string &condition_id);

        // Fetch orderbook
        std::optional<Orderbook> fetch_orderbook(const std::string &token_id);
        bool refresh_market_metadata(MarketState &market);

        // Fetch crypto up/down markets from Gamma API
        std::vector<MarketState> fetch_crypto_15m_markets();
        std::vector<MarketState> fetch_crypto_4h_markets();
        std::vector<MarketState> fetch_crypto_1h_markets();

        // Convert ClobMarket to MarketState
        static MarketState to_market_state(const ClobMarket &market);

    private:
        Config config_;
        HttpClient http_;

        // Timestamp generation for crypto markets
        std::vector<uint64_t> get_15m_timestamps(int count);
        std::vector<uint64_t> get_4h_timestamps(int count);
        std::vector<std::string> generate_1h_slugs(int count);
        std::vector<MarketState> fetch_timestamp_markets(
            const std::string &timeframe,
            const std::vector<uint64_t> &timestamps);
        std::vector<MarketState> fetch_gamma_markets(
            const std::vector<std::pair<std::string, std::string>> &requests);
        std::vector<ClobMarket> fetch_market_pages(std::size_t limit,
                                                   bool neg_risk_only);

        // Parse JSON responses
        std::vector<ClobMarket> parse_markets_response(const std::string &json);
        std::optional<Orderbook> parse_orderbook_response(
            const std::string &json, const std::string &expected_asset_id);
        std::optional<MarketState> parse_gamma_event(const std::string &json, const std::string &ticker);
    };

} // namespace polymarket
