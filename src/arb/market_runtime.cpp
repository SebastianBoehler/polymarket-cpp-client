#include "arb/market_runtime.hpp"
#include <regex>

namespace polymarket::arb
{

    std::vector<MarketState> fetch_markets(MarketFetcher &fetcher,
                                           const MarketDiscoveryOptions &options)
    {
        std::vector<MarketState> markets;
        if (options.fetch_15m)
        {
            auto batch = fetcher.fetch_crypto_15m_markets();
            markets.insert(markets.end(), batch.begin(), batch.end());
        }
        if (options.fetch_4h)
        {
            auto batch = fetcher.fetch_crypto_4h_markets();
            markets.insert(markets.end(), batch.begin(), batch.end());
        }
        if (options.fetch_1h)
        {
            auto batch = fetcher.fetch_crypto_1h_markets();
            markets.insert(markets.end(), batch.begin(), batch.end());
        }
        if (options.fetch_neg_risk)
        {
            auto clob_markets = fetcher.fetch_neg_risk_markets(options.max_markets);
            for (const auto &market : clob_markets)
            {
                markets.push_back(MarketFetcher::to_market_state(market));
            }
        }
        return markets;
    }

    uint64_t market_expiry_ms(const std::string &slug, const std::string &timeframe_hint)
    {
        std::regex ts_regex("-(\\d{10})$");
        std::smatch match;
        if (!std::regex_search(slug, match, ts_regex))
        {
            return 0;
        }

        uint64_t start_ts_ms = std::stoull(match[1].str()) * 1000ULL;
        if (timeframe_hint == "15m")
        {
            return start_ts_ms + (15ULL * 60ULL * 1000ULL);
        }
        if (timeframe_hint == "1h")
        {
            return start_ts_ms + (60ULL * 60ULL * 1000ULL);
        }
        if (timeframe_hint == "4h")
        {
            return start_ts_ms + (4ULL * 60ULL * 60ULL * 1000ULL);
        }
        return 0;
    }

    MarketState *select_best_market(std::vector<MarketState> &markets,
                                    const std::string &symbol,
                                    const std::string &timeframe_hint,
                                    uint64_t min_time_left_ms)
    {
        MarketState *best = nullptr;
        uint64_t best_expiry = 0;
        const uint64_t now_ms = now_sec() * 1000ULL;

        for (auto &market : markets)
        {
            if (market.symbol != symbol)
            {
                continue;
            }
            const uint64_t expiry_ms = market_expiry_ms(market.slug, timeframe_hint);
            if (expiry_ms == 0 || expiry_ms <= now_ms + min_time_left_ms)
            {
                continue;
            }
            if (!best || expiry_ms < best_expiry)
            {
                best = &market;
                best_expiry = expiry_ms;
            }
        }

        return best;
    }

    std::optional<PreparedMarket> prepare_market(ClobClient &client,
                                                 const MarketState &market,
                                                 const std::string &timeframe_hint)
    {
        auto tick_size = client.get_tick_size(market.token_yes);
        auto neg_risk = client.get_neg_risk(market.token_yes);
        if (!tick_size || !neg_risk)
        {
            return std::nullopt;
        }

        PreparedMarket prepared;
        prepared.market = market;
        try
        {
            prepared.metadata.tick_size = std::stod(tick_size->minimum_tick_size);
        }
        catch (...)
        {
            return std::nullopt;
        }
        prepared.metadata.neg_risk = neg_risk->neg_risk;
        prepared.expiry_ms = market_expiry_ms(market.slug, timeframe_hint);
        return prepared;
    }

} // namespace polymarket::arb
