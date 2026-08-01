#include "arb_runtime.hpp"

#include <atomic>
#include <csignal>
#include <iostream>
#include <thread>

using namespace polymarket;

namespace
{
    std::atomic<bool> running{true};

    void stop_signal(int)
    {
        running.store(false);
    }
}

int main(int argc, char **argv)
{
    bool help_requested = false;
    std::optional<arb::Options> options;
    try
    {
        options = arb::parse_options(argc, argv, help_requested);
    }
    catch (const std::exception &error)
    {
        std::cerr << "Invalid option value: " << error.what() << '\n';
        return 1;
    }
    if (help_requested)
    {
        arb::print_usage();
        return 0;
    }
    if (!options) return 1;

    std::signal(SIGINT, stop_signal);
    std::signal(SIGTERM, stop_signal);
    http_global_init();

    Config config;
    config.crypto_tickers = {options->symbol};
    config.max_markets = options->max_markets;
    config.trigger_combined = options->trigger;
    MarketFetcher fetcher(config);
    auto markets = arb::fetch_selected(fetcher, *options);
    if (markets.empty())
    {
        std::cerr << "No matching markets found\n";
        http_global_cleanup();
        return 1;
    }
    if (options->fetch_only)
    {
        arb::print_fetch_only(fetcher, markets);
        http_global_cleanup();
        return 0;
    }

    const bool crypto_selected = options->fetch_15m || options->fetch_1h || options->fetch_4h;
    const std::string target_symbol = crypto_selected ? options->symbol : "";
    const auto prepare_market = [&](MarketState *market)
    {
        return market && fetcher.refresh_market_metadata(*market);
    };
    MarketState *current = arb::select_market(markets, target_symbol);
    if (!prepare_market(current))
    {
        std::cerr << "No unexpired market with current CLOB metadata found\n";
        http_global_cleanup();
        return 1;
    }

    OrderbookManager books(config);
    books.on_arb_opportunity_with_permit(
        [&](const MarketState &market, double raw_combined,
            StreamGenerationPermit stream_permit)
    {
        if (!books.is_stream_current(stream_permit)) return;
        ArbSizingInput input;
        input.yes_ask = market.best_ask_yes;
        input.no_ask = market.best_ask_no;
        input.yes_available = market.best_ask_yes_size;
        input.no_available = market.best_ask_no_size;
        input.max_usdc_per_leg = options->size_usdc;
        input.tick_size = std::stod(market.minimum_tick_size);
        input.slippage = input.tick_size;
        input.minimum_order_size = market.minimum_order_size;
        input.fee_rate = market.fees_enabled ? market.fee_rate : 0.0;
        input.fee_exponent = market.fee_exponent;
        const auto plan = size_complementary_arb(input);
        std::cout << "\n[Arb] raw=" << raw_combined << " executable_edge=$" << plan.edge
                  << " shares=" << plan.yes_shares << '\n';
        if (!plan.executable)
        {
            std::cout << "[Arb] skipped: " << plan.reason << '\n';
            return;
        }
        if (!books.is_stream_current(stream_permit)) return;
        std::cout << "[Dry run] Analysis only; no orders submitted\n";
    });

    books.subscribe(*current);
    if (!books.connect())
    {
        std::cerr << "CLOB WebSocket handshake timed out\n";
        http_global_cleanup();
        return 1;
    }
    std::thread websocket_loop([&books]() { books.run(); });

    while (running.load())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        if (arb::seconds_until(current->end_time_ms, now_sec() * 1000) >= 60)
        {
            continue;
        }
        books.unsubscribe_all();
        markets = arb::fetch_selected(fetcher, *options);
        current = arb::select_market(markets, target_symbol);
        if (!prepare_market(current))
        {
            std::cerr << "No replacement market available\n";
            running.store(false);
            break;
        }
        std::cout << "[Market] Rotated to " << current->slug << '\n';
        books.subscribe(*current);
    }

    books.stop();
    if (websocket_loop.joinable()) websocket_loop.join();
    http_global_cleanup();
    return 0;
}
