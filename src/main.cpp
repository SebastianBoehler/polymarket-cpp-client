#include "arb/client_bootstrap.hpp"
#include "arb/engine.hpp"
#include "arb/market_runtime.hpp"
#include "arb/polymarket_execution_adapter.hpp"
#include "market_fetcher.hpp"
#include "orderbook.hpp"
#include <atomic>
#include <csignal>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <thread>
using namespace polymarket;
using namespace polymarket::arb;
namespace
{
    std::atomic<bool> g_running{true};
    struct CliOptions
    {
        bool fetch_only = false;
        bool dry_run = true;
        std::string symbol = "btc";
        std::string timeframe_hint = "15m";
        MarketDiscoveryOptions discovery;
        ArbConfig arb;
    };
    void signal_handler(int) { g_running.store(false); }
    void print_usage()
    {
        std::cout
            << "Usage: polymarket_arb [options]\n"
            << "  --help --fetch-only --15m|--1h|--4h --neg-risk --symbol SYMBOL --max N\n"
            << "  --trigger N --size-usdc N --slippage N --book-age-ms N --min-depth-ratio N\n"
            << "  --orphan-timeout-ms N --cooldown-ms N --dry-run --live\n"
            << std::endl;
    }
    CliOptions parse_cli(int argc, char *argv[])
    {
        CliOptions options;
        const auto set_timeframe = [&](bool use_15m, bool use_1h, bool use_4h, const std::string &hint)
        {
            options.discovery.fetch_15m = use_15m;
            options.discovery.fetch_1h = use_1h;
            options.discovery.fetch_4h = use_4h;
            options.timeframe_hint = hint;
        };
        for (int i = 1; i < argc; ++i)
        {
            const std::string arg = argv[i];
            if (arg == "--help")
            {
                print_usage();
                std::exit(0);
            }
            if (arg == "--fetch-only")
                options.fetch_only = true;
            else if (arg == "--15m")
                set_timeframe(true, false, false, "15m");
            else if (arg == "--1h")
                set_timeframe(false, true, false, "1h");
            else if (arg == "--4h")
                set_timeframe(false, false, true, "4h");
            else if (arg == "--neg-risk")
                options.discovery.fetch_neg_risk = true;
            else if (arg == "--symbol" && i + 1 < argc)
                options.symbol = argv[++i];
            else if (arg == "--max" && i + 1 < argc)
                options.discovery.max_markets = std::stoi(argv[++i]);
            else if (arg == "--trigger" && i + 1 < argc)
                options.arb.trigger_threshold = std::stod(argv[++i]);
            else if (arg == "--size-usdc" && i + 1 < argc)
                options.arb.per_leg_size_usdc = std::stod(argv[++i]);
            else if (arg == "--slippage" && i + 1 < argc)
                options.arb.slippage_buffer = std::stod(argv[++i]);
            else if (arg == "--book-age-ms" && i + 1 < argc)
                options.arb.max_book_age_ms = std::stoull(argv[++i]);
            else if (arg == "--min-depth-ratio" && i + 1 < argc)
                options.arb.minimum_visible_depth_ratio = std::stod(argv[++i]);
            else if (arg == "--orphan-timeout-ms" && i + 1 < argc)
                options.arb.orphan_unwind_timeout_ms = std::stoull(argv[++i]);
            else if (arg == "--cooldown-ms" && i + 1 < argc)
                options.arb.cooldown_ms = std::stoull(argv[++i]);
            else if (arg == "--dry-run")
                options.dry_run = true;
            else if (arg == "--live")
                options.dry_run = false;
        }
        if (const char *size_env = std::getenv("SIZE_USDC"))
            options.arb.per_leg_size_usdc = std::stod(size_env);
        if (const char *dry_run_env = std::getenv("DRY_RUN"))
            options.dry_run = std::string(dry_run_env) != "false";
        return options;
    }
    void print_report(const ExecutionReport &report)
    {
        std::cout << "\n\n[Execution] " << report.signal_id << '\n'
                  << "  both_filled=" << std::boolalpha << report.both_filled
                  << " one_filled=" << report.one_filled
                  << " both_rejected=" << report.both_rejected
                  << " dry_run=" << report.dry_run << '\n'
                  << "  YES status=" << report.yes_leg.status
                  << " filled=" << report.yes_leg.filled
                  << " error=" << report.yes_leg.error_msg << '\n'
                  << "  NO  status=" << report.no_leg.status
                  << " filled=" << report.no_leg.filled
                  << " error=" << report.no_leg.error_msg << '\n';
        if (report.flatten_report.has_value())
        {
            std::cout << "  flatten success=" << report.flatten_report->success
                      << " error=" << report.flatten_report->error_msg << '\n';
        }
        if (!report.cooldown_reason.empty())
        {
            std::cout << "  policy=" << report.cooldown_reason << '\n';
        }
    }
} // namespace

int main(int argc, char *argv[])
{
    const auto options = parse_cli(argc, argv);
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    http_global_init();

    Config runtime_config;
    runtime_config.max_markets = options.discovery.max_markets;
    runtime_config.trigger_combined = options.arb.trigger_threshold;

    MarketFetcher fetcher(runtime_config);
    auto markets = fetch_markets(fetcher, options.discovery);
    if (markets.empty())
    {
        std::cerr << "[Error] No markets found." << std::endl;
        http_global_cleanup();
        return 1;
    }

    if (options.fetch_only)
    {
        std::cout << "[FetchOnly] Markets fetched: " << markets.size() << std::endl;
        http_global_cleanup();
        return 0;
    }

    ClobClient public_client(runtime_config.clob_rest_url, 137);
    std::shared_ptr<ClobClient> live_client;
    if (!options.dry_run)
    {
        const char *private_key = std::getenv("PRIVATE_KEY");
        if (!private_key)
        {
            std::cerr << "[Error] PRIVATE_KEY is required for live mode." << std::endl;
            http_global_cleanup();
            return 1;
        }
        const char *funder = std::getenv("FUNDER_ADDRESS");
        const auto session = create_authenticated_session(
            runtime_config.clob_rest_url,
            137,
            private_key,
            funder ? funder : "",
            api_credentials_from_env());
        live_client = session.client;
        if (!live_client->warm_connection())
        {
            std::cerr << "[Error] Failed to warm Polymarket REST connection." << std::endl;
            http_global_cleanup();
            return 1;
        }
        live_client->start_heartbeat();
    }

    auto switch_market = [&]() -> std::optional<PreparedMarket>
    {
        auto *selected = select_best_market(markets, options.symbol, options.timeframe_hint);
        if (!selected)
        {
            markets = fetch_markets(fetcher, options.discovery);
            selected = select_best_market(markets, options.symbol, options.timeframe_hint);
        }
        if (!selected)
        {
            return std::nullopt;
        }
        return prepare_market(public_client, *selected, options.timeframe_hint);
    };

    const auto initial_market = switch_market();
    if (!initial_market)
    {
        std::cerr << "[Error] Failed to prepare a market: missing tick size or neg-risk metadata."
                  << std::endl;
        if (live_client)
            live_client->stop_heartbeat();
        http_global_cleanup();
        return 1;
    }

    auto adapter = std::make_unique<PolymarketExecutionAdapter>(live_client, options.dry_run);
    ArbEngine engine(options.arb, std::move(adapter));
    engine.set_active_market(*initial_market);

    OrderbookManager orderbook_mgr(runtime_config);
    orderbook_mgr.subscribe(initial_market->market);
    orderbook_mgr.on_orderbook_update([&](const std::string &, const Orderbook &)
                                      {
        const auto active = engine.active_market();
        if (!active)
            return;

        const auto yes_book = orderbook_mgr.get_orderbook(active->market.token_yes);
        const auto no_book = orderbook_mgr.get_orderbook(active->market.token_no);
        if (yes_book && no_book)
        {
            engine.on_orderbooks(*yes_book, *no_book);
        } });

    if (!orderbook_mgr.connect())
    {
        std::cerr << "[Error] Failed to connect orderbook manager." << std::endl;
        if (live_client)
            live_client->stop_heartbeat();
        http_global_cleanup();
        return 1;
    }

    std::thread ws_thread([&orderbook_mgr]()
                          { orderbook_mgr.run(); });

    std::string last_report_id;
    while (g_running.load())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        const auto active = engine.active_market();
        if (!active)
        {
            break;
        }

        const auto state = orderbook_mgr.get_market(active->market.condition_id);
        const int64_t ttl_sec = active->expiry_ms == 0
                                    ? -1
                                    : static_cast<int64_t>(active->expiry_ms - (now_sec() * 1000ULL)) / 1000;
        const auto policy = engine.block_reason(now_ns());
        if (state.best_ask_yes > 0.0 && state.best_ask_no > 0.0)
        {
            std::cout << "\r[" << active->market.slug << "] YES="
                      << std::fixed << std::setprecision(4) << state.best_ask_yes
                      << " NO=" << state.best_ask_no
                      << " SUM=" << (state.best_ask_yes + state.best_ask_no)
                      << " TTL=" << ttl_sec
                      << " policy=" << (policy.empty() ? "ready" : policy)
                      << "    " << std::flush;
        }

        const auto report = engine.last_report();
        if (report && report->signal_id != last_report_id)
        {
            last_report_id = report->signal_id;
            print_report(*report);
        }

        if (ttl_sec >= 0 && ttl_sec < 60)
        {
            std::cout << "\n[Market] Switching expiring market..." << std::endl;
            orderbook_mgr.unsubscribe_all();
            const auto next_market = switch_market();
            if (!next_market)
            {
                std::cerr << "[Error] No replacement market available." << std::endl;
                break;
            }
            engine.set_active_market(*next_market);
            orderbook_mgr.subscribe(next_market->market);
        }
    }

    orderbook_mgr.stop();
    if (ws_thread.joinable())
        ws_thread.join();
    if (live_client)
        live_client->stop_heartbeat();

    const auto counters = engine.counters();
    std::cout << "\n[Summary] opportunities_seen=" << counters.opportunities_seen
              << " opportunities_skipped=" << counters.opportunities_skipped
              << " orders_submitted=" << counters.orders_submitted
              << " both_filled=" << counters.both_filled
              << " one_filled=" << counters.one_filled
              << " both_rejected=" << counters.both_rejected << std::endl;

    http_global_cleanup();
    return 0;
}
