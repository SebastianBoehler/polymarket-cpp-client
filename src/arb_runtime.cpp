#include "arb_runtime.hpp"

#include <cstdlib>
#include <cctype>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace polymarket::arb
{
    namespace
    {
        void append_unique(std::vector<MarketState> &destination, std::vector<MarketState> source)
        {
            std::unordered_set<std::string> existing;
            for (const auto &market : destination) existing.insert(market.condition_id);
            for (auto &market : source)
            {
                if (!market.condition_id.empty() && existing.insert(market.condition_id).second)
                {
                    destination.push_back(std::move(market));
                }
            }
        }

        bool filled(const OrderResponse &response)
        {
            return response.success && response.error_msg.empty() && response.status == "matched";
        }

        int parse_positive_int(const std::string &value, const char *name)
        {
            std::size_t consumed = 0;
            const long long parsed = std::stoll(value, &consumed);
            if (consumed != value.size() || parsed <= 0 ||
                parsed > std::numeric_limits<int>::max())
                throw std::invalid_argument(std::string(name) + " must be a positive integer");
            return static_cast<int>(parsed);
        }

        double parse_positive_finite(const std::string &value, const char *name)
        {
            std::size_t consumed = 0;
            const double parsed = std::stod(value, &consumed);
            if (consumed != value.size() || !std::isfinite(parsed) || parsed <= 0.0)
                throw std::invalid_argument(std::string(name) + " must be finite and positive");
            return parsed;
        }
    }

    void print_usage()
    {
        std::cout << "Usage: polymarket_arb [--15m|--1h|--4h|--neg-risk] [options]\n"
                     "  --fetch-only       Fetch markets and current books, then exit\n"
                     "  --max N            Maximum markets to retain (default 50)\n"
                     "  --symbol TICKER    Crypto symbol to discover (default btc)\n"
                     "  --trigger N        Raw combined ask callback threshold (default 0.98)\n"
                     "  --dry-run           Analyze opportunities without submitting (default)\n"
                     "  --live              Disabled: paired CLOB orders are not atomic\n\n"
                     "SIZE_USDC controls the per-leg analysis budget.\n";
    }

    std::optional<Options> parse_options(int argc, char **argv, bool &help_requested)
    {
        Options options;
        help_requested = false;
        for (int index = 1; index < argc; ++index)
        {
            const std::string argument = argv[index];
            if (argument == "--help")
            {
                help_requested = true;
                return std::nullopt;
            }
            if (argument == "--fetch-only") options.fetch_only = true;
            else if (argument == "--15m") options.fetch_15m = true;
            else if (argument == "--1h") options.fetch_1h = true;
            else if (argument == "--4h") options.fetch_4h = true;
            else if (argument == "--neg-risk") options.fetch_neg_risk = true;
            else if (argument == "--dry-run") {}
            else if (argument == "--live")
            {
                throw std::invalid_argument(
                    "--live is disabled because paired CLOB orders are not atomic");
            }
            else if (argument == "--max" && index + 1 < argc)
                options.max_markets = parse_positive_int(argv[++index], "--max");
            else if (argument == "--symbol" && index + 1 < argc) options.symbol = argv[++index];
            else if (argument == "--trigger" && index + 1 < argc)
                options.trigger = parse_positive_finite(argv[++index], "--trigger");
            else
            {
                std::cerr << "Unknown or incomplete option: " << argument << '\n';
                return std::nullopt;
            }
        }
        if (!options.fetch_15m && !options.fetch_1h && !options.fetch_4h && !options.fetch_neg_risk)
        {
            options.fetch_15m = true;
        }
        if (const char *size = std::getenv("SIZE_USDC"))
            options.size_usdc = parse_positive_finite(size, "SIZE_USDC");
        std::transform(options.symbol.begin(), options.symbol.end(), options.symbol.begin(),
                       [](unsigned char value)
                       { return static_cast<char>(std::tolower(value)); });
        const bool valid_symbol = !options.symbol.empty() &&
                                  std::all_of(options.symbol.begin(), options.symbol.end(),
                                              [](unsigned char value)
                                              { return std::isalnum(value) != 0; });
        if (!valid_symbol || options.max_markets <= 0 || options.size_usdc <= 0.0 ||
            options.trigger <= 0.0)
        {
            std::cerr << "symbol must be alphanumeric; max, size, and trigger must be positive\n";
            return std::nullopt;
        }
        return options;
    }

    std::vector<MarketState> fetch_selected(MarketFetcher &fetcher, const Options &options)
    {
        std::vector<MarketState> markets;
        if (options.fetch_15m) append_unique(markets, fetcher.fetch_crypto_15m_markets());
        if (options.fetch_1h) append_unique(markets, fetcher.fetch_crypto_1h_markets());
        if (options.fetch_4h) append_unique(markets, fetcher.fetch_crypto_4h_markets());
        if (options.fetch_neg_risk)
        {
            for (const auto &market : fetcher.fetch_neg_risk_markets(options.max_markets))
            {
                append_unique(markets, {MarketFetcher::to_market_state(market)});
            }
        }
        if (markets.size() > static_cast<std::size_t>(options.max_markets)) markets.resize(options.max_markets);
        return markets;
    }

    MarketState *select_market(std::vector<MarketState> &markets, const std::string &symbol)
    {
        const uint64_t earliest = now_sec() * 1000 + 2 * 60 * 1000;
        MarketState *best = nullptr;
        for (auto &market : markets)
        {
            if ((!symbol.empty() && market.symbol != symbol) || market.end_time_ms <= earliest) continue;
            if (!best || market.end_time_ms < best->end_time_ms) best = &market;
        }
        return best;
    }

    int64_t seconds_until(uint64_t end_time_ms, uint64_t now_ms)
    {
        if (end_time_ms >= now_ms) return static_cast<int64_t>((end_time_ms - now_ms) / 1000);
        return -static_cast<int64_t>((now_ms - end_time_ms + 999) / 1000);
    }

    BatchOutcome assess_batch(const std::vector<OrderResponse> &responses)
    {
        if (responses.size() != 2) return BatchOutcome::Unsafe;
        const bool yes_filled = filled(responses[0]);
        const bool no_filled = filled(responses[1]);
        if (yes_filled && no_filled) return BatchOutcome::BothFilled;
        if (!responses[0].success && !responses[1].success) return BatchOutcome::BothRejected;
        return BatchOutcome::Unsafe;
    }

    bool reconcile_order_metadata(MarketState &market,
                                  const TickSizeInfo &yes_tick,
                                  const TickSizeInfo &no_tick,
                                  const NegRiskInfo &yes_neg_risk,
                                  const NegRiskInfo &no_neg_risk)
    {
        if (yes_tick.minimum_tick_size.empty() ||
            yes_tick.minimum_tick_size != no_tick.minimum_tick_size ||
            yes_neg_risk.neg_risk != no_neg_risk.neg_risk)
        {
            return false;
        }
        market.minimum_tick_size = yes_tick.minimum_tick_size;
        market.neg_risk = yes_neg_risk.neg_risk;
        return true;
    }

    void print_fetch_only(MarketFetcher &fetcher, const std::vector<MarketState> &markets)
    {
        for (const auto &market : markets)
        {
            const auto yes = fetcher.fetch_orderbook(market.token_yes);
            const auto no = fetcher.fetch_orderbook(market.token_no);
            if (yes && no)
            {
                std::cout << market.slug << " YES=" << yes->best_ask() << " NO=" << no->best_ask()
                          << " SUM=" << yes->best_ask() + no->best_ask() << '\n';
            }
        }
    }
}
