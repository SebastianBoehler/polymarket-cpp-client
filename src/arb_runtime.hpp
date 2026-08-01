#pragma once

#include "clob_client.hpp"
#include "market_fetcher.hpp"
#include "orderbook.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace polymarket::arb
{
    struct Options
    {
        bool fetch_only{false};
        bool fetch_15m{false};
        bool fetch_1h{false};
        bool fetch_4h{false};
        bool fetch_neg_risk{false};
        std::string symbol{"btc"};
        int max_markets{50};
        double trigger{0.98};
        double size_usdc{5.0};
    };

    enum class BatchOutcome
    {
        BothFilled,
        BothRejected,
        Unsafe
    };

    void print_usage();
    std::optional<Options> parse_options(int argc, char **argv, bool &help_requested);
    std::vector<MarketState> fetch_selected(MarketFetcher &fetcher, const Options &options);
    MarketState *select_market(std::vector<MarketState> &markets, const std::string &symbol);
    int64_t seconds_until(uint64_t end_time_ms, uint64_t now_ms);
    BatchOutcome assess_batch(const std::vector<OrderResponse> &responses);
    bool reconcile_order_metadata(MarketState &market,
                                  const TickSizeInfo &yes_tick,
                                  const TickSizeInfo &no_tick,
                                  const NegRiskInfo &yes_neg_risk,
                                  const NegRiskInfo &no_neg_risk);
    void print_fetch_only(MarketFetcher &fetcher, const std::vector<MarketState> &markets);
}
