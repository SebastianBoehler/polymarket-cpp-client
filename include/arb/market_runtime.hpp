#pragma once

#include "arb/arb_types.hpp"
#include "market_fetcher.hpp"
#include <optional>
#include <string>
#include <vector>

namespace polymarket::arb
{

    struct MarketDiscoveryOptions
    {
        bool fetch_15m = true;
        bool fetch_4h = false;
        bool fetch_1h = false;
        bool fetch_neg_risk = false;
        int max_markets = 50;
    };

    std::vector<MarketState> fetch_markets(MarketFetcher &fetcher,
                                           const MarketDiscoveryOptions &options);

    uint64_t market_expiry_ms(const std::string &slug,
                              const std::string &timeframe_hint);

    MarketState *select_best_market(std::vector<MarketState> &markets,
                                    const std::string &symbol,
                                    const std::string &timeframe_hint,
                                    uint64_t min_time_left_ms = 120000);

    std::optional<PreparedMarket> prepare_market(ClobClient &client,
                                                 const MarketState &market,
                                                 const std::string &timeframe_hint);

} // namespace polymarket::arb
