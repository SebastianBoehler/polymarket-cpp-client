#pragma once

#include "types.hpp"
#include "http_client.hpp"
#include <vector>
#include <optional>

namespace polymarket
{

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

        // Parse JSON responses
        std::vector<ClobMarket> parse_markets_response(const std::string &json);
        std::optional<Orderbook> parse_orderbook_response(const std::string &json);
        std::optional<MarketState> parse_gamma_event(const std::string &json, const std::string &ticker);
    };

} // namespace polymarket
