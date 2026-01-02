#include "types.hpp"
#include "http_client.hpp"
#include "market_fetcher.hpp"
#include "orderbook.hpp"
#include "order_signer.hpp"
#include <iostream>
#include <csignal>
#include <thread>
#include <atomic>
#include <iomanip>
#include <regex>
#include <cstdlib>

using namespace polymarket;

// Extract market expiry timestamp from slug (e.g., "btc-updown-15m-1767170700")
uint64_t get_market_expiry(const std::string &slug, const std::string &timeframe = "15m")
{
    std::regex ts_regex("-(\\d{10})$");
    std::smatch match;
    if (std::regex_search(slug, match, ts_regex))
    {
        uint64_t start_ts = std::stoull(match[1].str()) * 1000; // to ms
        if (timeframe == "15m")
            return start_ts + 15 * 60 * 1000;
        if (timeframe == "1h")
            return start_ts + 60 * 60 * 1000;
        if (timeframe == "4h")
            return start_ts + 4 * 60 * 60 * 1000;
        return start_ts + 15 * 60 * 1000;
    }
    // Fallback
    return now_sec() * 1000 + 15 * 60 * 1000;
}

// Global flag for graceful shutdown
std::atomic<bool> g_running{true};

// Pre-fetched market config for fast order placement
struct MarketConfig
{
    std::string tick_size = "0.01";
    bool neg_risk = true;
};
std::atomic<bool> g_config_ready{false};
MarketConfig g_market_config;

void signal_handler(int signal)
{
    std::cout << "\n[Main] Received signal " << signal << ", shutting down..." << std::endl;
    g_running.store(false);
}

void print_usage()
{
    std::cout << "Polymarket Arbitrage Bot (C++ Edition)\n"
              << "======================================\n\n"
              << "Usage: polymarket_arb [options]\n\n"
              << "Options:\n"
              << "  --help          Show this help message\n"
              << "  --fetch-only    Only fetch markets, don't subscribe to WebSocket\n"
              << "  --15m           Fetch 15-minute crypto markets\n"
              << "  --4h            Fetch 4-hour crypto markets\n"
              << "  --1h            Fetch 1-hour crypto markets\n"
              << "  --neg-risk      Fetch neg_risk binary markets (default)\n"
              << "  --max N         Maximum number of markets to fetch (default: 50)\n"
              << "  --trigger N     Trigger threshold for arb (default: 0.98)\n"
              << "  --dry-run       Don't place actual orders (default)\n"
              << "  --live          Place actual orders (requires PRIVATE_KEY, API_KEY, etc)\n"
              << "\nEnvironment variables for live trading:\n"
              << "  PRIVATE_KEY     - Wallet private key\n"
              << "  FUNDER_ADDRESS  - Address holding funds (for proxy wallets)\n"
              << "  API_KEY         - Polymarket API key\n"
              << "  API_SECRET      - Polymarket API secret\n"
              << "  API_PASSPHRASE  - Polymarket API passphrase\n"
              << "  SIZE_USDC       - Size per leg in USDC (default: 5)\n"
              << std::endl;
}

int main(int argc, char *argv[])
{
    // Parse command line arguments
    bool fetch_only = false;
    bool fetch_15m = false;
    bool fetch_4h = false;
    bool fetch_1h = false;
    bool fetch_neg_risk = false;
    int max_markets = 50;
    double trigger = 0.98;
    bool dry_run = true;
    double size_usdc = 5.0;

    for (int i = 1; i < argc; i++)
    {
        std::string arg = argv[i];
        if (arg == "--help")
        {
            print_usage();
            return 0;
        }
        else if (arg == "--fetch-only")
        {
            fetch_only = true;
        }
        else if (arg == "--15m")
        {
            fetch_15m = true;
        }
        else if (arg == "--4h")
        {
            fetch_4h = true;
        }
        else if (arg == "--1h")
        {
            fetch_1h = true;
        }
        else if (arg == "--neg-risk")
        {
            fetch_neg_risk = true;
        }
        else if (arg == "--max" && i + 1 < argc)
        {
            max_markets = std::stoi(argv[++i]);
        }
        else if (arg == "--trigger" && i + 1 < argc)
        {
            trigger = std::stod(argv[++i]);
        }
        else if (arg == "--dry-run")
        {
            dry_run = true;
        }
        else if (arg == "--live")
        {
            dry_run = false;
        }
    }

    // Check environment variables
    const char *env_size = std::getenv("SIZE_USDC");
    if (env_size)
        size_usdc = std::stod(env_size);

    const char *env_dry_run = std::getenv("DRY_RUN");
    if (env_dry_run && std::string(env_dry_run) == "false")
        dry_run = false;

    // Default to 15m crypto markets if nothing specified (like arb-smoke.ts)
    if (!fetch_15m && !fetch_4h && !fetch_1h && !fetch_neg_risk)
    {
        fetch_15m = true;
    }

    // Setup signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Initialize HTTP
    http_global_init();

    std::cout << "╔══════════════════════════════════════════════════════════════╗\n"
              << "║       Polymarket Arbitrage Bot (C++ Low-Latency Edition)     ║\n"
              << "╚══════════════════════════════════════════════════════════════╝\n"
              << std::endl;

    // Configuration
    Config config;
    config.max_markets = max_markets;
    config.trigger_combined = trigger;

    std::cout << "[Config] Trigger threshold: " << std::fixed << std::setprecision(2)
              << config.trigger_combined << std::endl;
    std::cout << "[Config] Max markets: " << config.max_markets << std::endl;
    std::cout << "[Config] Size per leg: $" << size_usdc << std::endl;
    std::cout << "[Config] Mode: " << (dry_run ? "DRY RUN" : "LIVE TRADING") << std::endl;
    std::cout << std::endl;

    // Initialize order signer if live trading
    std::unique_ptr<OrderSigner> order_signer;
    ApiCredentials api_creds;

    if (!dry_run)
    {
        const char *private_key = std::getenv("PRIVATE_KEY");
        const char *funder_address = std::getenv("FUNDER_ADDRESS");
        const char *api_key = std::getenv("API_KEY");
        const char *api_secret = std::getenv("API_SECRET");
        const char *api_passphrase = std::getenv("API_PASSPHRASE");

        if (!private_key || !api_key || !api_secret || !api_passphrase)
        {
            std::cerr << "[Error] Live trading requires PRIVATE_KEY, API_KEY, API_SECRET, API_PASSPHRASE" << std::endl;
            return 1;
        }

        try
        {
            order_signer = std::make_unique<OrderSigner>(private_key, 137);
            std::cout << "[Signer] Initialized, address: " << order_signer->address() << std::endl;

            api_creds.api_key = api_key;
            api_creds.api_secret = api_secret;
            api_creds.api_passphrase = api_passphrase;

            if (funder_address)
            {
                std::cout << "[Signer] Funder address: " << funder_address << std::endl;
            }
        }
        catch (const std::exception &e)
        {
            std::cerr << "[Error] Failed to initialize signer: " << e.what() << std::endl;
            return 1;
        }
    }

    // Fetch markets
    MarketFetcher fetcher(config);
    std::vector<MarketState> markets;

    if (fetch_15m)
    {
        auto m = fetcher.fetch_crypto_15m_markets();
        markets.insert(markets.end(), m.begin(), m.end());
    }

    if (fetch_4h)
    {
        auto m = fetcher.fetch_crypto_4h_markets();
        markets.insert(markets.end(), m.begin(), m.end());
    }

    if (fetch_1h)
    {
        auto m = fetcher.fetch_crypto_1h_markets();
        markets.insert(markets.end(), m.begin(), m.end());
    }

    if (fetch_neg_risk)
    {
        auto clob_markets = fetcher.fetch_neg_risk_markets(config.max_markets);
        for (const auto &m : clob_markets)
        {
            markets.push_back(MarketFetcher::to_market_state(m));
        }
    }

    if (markets.empty())
    {
        std::cerr << "[Error] No markets found!" << std::endl;
        http_global_cleanup();
        return 1;
    }

    std::cout << "\n[Markets] Total markets to monitor: " << markets.size() << std::endl;

    // Print market summary
    std::cout << "\n┌─────────────────────────────────────────────────────────────────┐\n";
    std::cout << "│ Market Summary                                                  │\n";
    std::cout << "├─────────────────────────────────────────────────────────────────┤\n";
    for (size_t i = 0; i < std::min(markets.size(), size_t(10)); i++)
    {
        const auto &m = markets[i];
        std::string title = m.title.length() > 50 ? m.title.substr(0, 47) + "..." : m.title;
        std::cout << "│ " << std::left << std::setw(62) << title << " │\n";
    }
    if (markets.size() > 10)
    {
        std::cout << "│ ... and " << (markets.size() - 10) << " more markets"
                  << std::string(50 - std::to_string(markets.size() - 10).length(), ' ') << " │\n";
    }
    std::cout << "└─────────────────────────────────────────────────────────────────┘\n";
    std::cout << std::endl;

    if (fetch_only)
    {
        std::cout << "[Mode] Fetch-only mode, exiting..." << std::endl;

        // Fetch initial orderbooks for each market
        std::cout << "\n[Orderbooks] Fetching initial orderbook snapshots...\n"
                  << std::endl;

        for (const auto &market : markets)
        {
            auto book_yes = fetcher.fetch_orderbook(market.token_yes);
            auto book_no = fetcher.fetch_orderbook(market.token_no);

            if (book_yes && book_no)
            {
                double combined = book_yes->best_ask() + book_no->best_ask();
                std::cout << "  " << std::left << std::setw(12) << market.symbol
                          << " YES: " << std::fixed << std::setprecision(3) << book_yes->best_ask()
                          << " NO: " << std::setprecision(3) << book_no->best_ask()
                          << " Combined: " << std::setprecision(4) << combined;

                if (combined < config.trigger_combined)
                {
                    std::cout << " *** ARB OPPORTUNITY ***";
                }
                std::cout << std::endl;
            }
        }

        http_global_cleanup();
        return 0;
    }

    // Filter to get the best market (soonest expiring with enough time left)
    auto get_best_market = [](const std::vector<MarketState> &all_markets, const std::string &symbol) -> MarketState *
    {
        uint64_t now_ms = now_sec() * 1000;
        uint64_t min_time_left = 2 * 60 * 1000; // At least 2 min left

        MarketState *best = nullptr;
        uint64_t best_expiry = UINT64_MAX;

        for (auto &m : const_cast<std::vector<MarketState> &>(all_markets))
        {
            if (m.symbol != symbol)
                continue;
            uint64_t expiry = get_market_expiry(m.slug);
            if (expiry > now_ms + min_time_left && expiry < best_expiry)
            {
                best_expiry = expiry;
                best = &m;
            }
        }
        return best;
    };

    // Pick initial market (BTC by default, like arb-smoke.ts)
    std::string target_symbol = "btc";
    MarketState *current_market = get_best_market(markets, target_symbol);

    if (!current_market)
    {
        std::cerr << "[Error] No valid " << target_symbol << " market found!" << std::endl;
        http_global_cleanup();
        return 1;
    }

    uint64_t market_expiry = get_market_expiry(current_market->slug);
    int64_t time_left_sec = (market_expiry - now_sec() * 1000) / 1000;
    std::cout << "\n[Market] Using: " << current_market->slug
              << " (expires in " << time_left_sec << "s)" << std::endl;

    // Prefetch tick size and neg_risk for fast order placement
    if (!dry_run)
    {
        std::cout << "[Prefetch] Fetching tick size and neg_risk..." << std::endl;
        auto book = fetcher.fetch_orderbook(current_market->token_yes);
        if (book)
        {
            g_market_config.tick_size = "0.01"; // Default for crypto markets
            g_market_config.neg_risk = true;    // Crypto markets are neg_risk
            g_config_ready.store(true);
            std::cout << "[Prefetch] tickSize=" << g_market_config.tick_size
                      << ", negRisk=" << (g_market_config.neg_risk ? "true" : "false") << std::endl;
        }
    }

    // Create orderbook manager
    OrderbookManager orderbook_mgr(config);

    // Set up arb opportunity callback
    orderbook_mgr.on_arb_opportunity([&config, &dry_run, &size_usdc, &order_signer, &api_creds, &fetcher, &current_market](const LiveMarketState &market, double combined)
                                     {
        double edge = 1.0 - combined;
        double edge_pct = edge * 100.0;
        double slippage_buffer = 0.005; // 0.5% slippage per side
        
        double yes_price = std::min(market.best_ask_yes.load() + slippage_buffer, 0.99);
        double no_price = std::min(market.best_ask_no.load() + slippage_buffer, 0.99);
        
        // Round to 2 decimals for API compliance
        yes_price = std::round(yes_price * 100) / 100;
        no_price = std::round(no_price * 100) / 100;
        
        std::cout << "\n\n🎯 OPPORTUNITY FOUND! Combined=" << std::fixed << std::setprecision(4) 
                  << combined << " < " << config.trigger_combined << std::endl;
        std::cout << "  Market: " << market.slug << std::endl;
        std::cout << "  YES Ask: " << market.best_ask_yes.load() << " -> order @ " << yes_price << std::endl;
        std::cout << "  NO Ask:  " << market.best_ask_no.load() << " -> order @ " << no_price << std::endl;
        std::cout << "  Edge: " << std::setprecision(2) << edge_pct << "%" << std::endl;
        std::cout << "  Size: $" << size_usdc << " per leg" << std::endl;
        
        if (dry_run) {
            std::cout << "  [DRY RUN] Would place orders here\n" << std::endl;
            return;
        }
        
        if (!order_signer || !g_config_ready.load()) {
            std::cout << "  [ERROR] Order signer not ready\n" << std::endl;
            return;
        }
        
        // Calculate shares
        double yes_shares = std::floor((size_usdc / yes_price) * 100) / 100;
        double no_shares = std::floor((size_usdc / no_price) * 100) / 100;
        
        std::cout << "  [EXECUTING] Creating orders..." << std::endl;
        std::cout << "    YES: " << yes_shares << " shares @ " << yes_price << std::endl;
        std::cout << "    NO:  " << no_shares << " shares @ " << no_price << std::endl;
        
        // Create and sign orders
        // Note: Full order placement would require posting to API with L2 headers
        // This is a placeholder showing the signing works
        try {
            OrderData yes_order;
            yes_order.maker = order_signer->address();
            yes_order.taker = "0x0000000000000000000000000000000000000000";
            yes_order.token_id = market.token_yes;
            yes_order.maker_amount = to_wei(size_usdc, 6);
            yes_order.taker_amount = to_wei(yes_shares, 6);
            yes_order.side = OrderSide::BUY;
            yes_order.fee_rate_bps = "0";
            yes_order.nonce = "0";
            yes_order.signer = order_signer->address();
            yes_order.expiration = "0";
            yes_order.signature_type = SignatureType::EOA;
            
            auto signed_yes = order_signer->sign_order(yes_order, "0xC5d563A36AE78145C45a50134d48A1215220f80a");
            std::cout << "    YES order signed: " << signed_yes.signature.substr(0, 20) << "..." << std::endl;
            
            // TODO: Post orders to API with L2 headers
            // HttpClient http;
            // http.set_base_url("https://clob.polymarket.com");
            // auto headers = order_signer->generate_l2_headers(api_creds, "POST", "/order", body);
            // auto response = http.post("/order", body, headers);
            
            std::cout << "  [TODO] Order posting not yet implemented\n" << std::endl;
        } catch (const std::exception& e) {
            std::cout << "  [ERROR] Order signing failed: " << e.what() << "\n" << std::endl;
        } });

    // Subscribe to current market only
    std::vector<MarketState> current_markets = {*current_market};
    orderbook_mgr.subscribe(current_markets);

    // Connect to WebSocket
    std::cout << "[WebSocket] Connecting to orderbook stream..." << std::endl;

    if (!orderbook_mgr.connect())
    {
        std::cerr << "[Error] Failed to connect to WebSocket!" << std::endl;
        http_global_cleanup();
        return 1;
    }

    // Run WebSocket event loop in a separate thread
    std::thread ws_thread([&orderbook_mgr]()
                          { orderbook_mgr.run(); });

    // Main loop - monitor prices and check for market expiry
    while (g_running.load())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        uint64_t now_ms = now_sec() * 1000;
        int64_t time_left = (market_expiry - now_ms) / 1000;

        // Get current market state
        MarketState state = orderbook_mgr.get_market(current_market->condition_id);
        double combined = state.best_ask_yes + state.best_ask_no;

        // Print status line (overwrite previous)
        if (state.best_ask_yes > 0 && state.best_ask_no > 0)
        {
            std::cout << "\r[" << current_market->slug << "] "
                      << "YES=" << std::fixed << std::setprecision(4) << state.best_ask_yes
                      << " NO=" << state.best_ask_no
                      << " SUM=" << combined
                      << " (trigger <" << config.trigger_combined << ")"
                      << " TTL=" << time_left << "s   " << std::flush;
        }

        // Check for market expiry - switch 60s before
        if (time_left < 60)
        {
            std::cout << "\n\n⏰ Market expiring soon, switching..." << std::endl;

            // Stop current subscription
            orderbook_mgr.unsubscribe_all();

            // Re-fetch markets to get fresh data
            auto fresh_markets = fetcher.fetch_crypto_15m_markets();
            if (!fresh_markets.empty())
            {
                markets = fresh_markets;
            }

            // Find next market
            current_market = get_best_market(markets, target_symbol);
            if (!current_market)
            {
                std::cout << "[Warn] No more " << target_symbol << " markets, waiting 30s..." << std::endl;
                std::this_thread::sleep_for(std::chrono::seconds(30));

                // Try again
                fresh_markets = fetcher.fetch_crypto_15m_markets();
                if (!fresh_markets.empty())
                {
                    markets = fresh_markets;
                    current_market = get_best_market(markets, target_symbol);
                }

                if (!current_market)
                {
                    std::cerr << "[Error] Still no markets available, exiting." << std::endl;
                    break;
                }
            }

            market_expiry = get_market_expiry(current_market->slug);
            time_left_sec = (market_expiry - now_sec() * 1000) / 1000;
            std::cout << "[Market] Switched to: " << current_market->slug
                      << " (expires in " << time_left_sec << "s)" << std::endl;

            // Prefetch tick size for new market
            if (!dry_run)
            {
                auto book = fetcher.fetch_orderbook(current_market->token_yes);
                if (book)
                {
                    g_market_config.tick_size = "0.01";
                    g_market_config.neg_risk = true;
                    g_config_ready.store(true);
                    std::cout << "[Prefetch] tickSize=" << g_market_config.tick_size << std::endl;
                }
            }

            // Subscribe to new market
            current_markets = {*current_market};
            orderbook_mgr.subscribe(current_markets);
        }
    }

    // Shutdown
    std::cout << "\n[Main] Stopping orderbook manager..." << std::endl;
    orderbook_mgr.stop();

    if (ws_thread.joinable())
    {
        ws_thread.join();
    }

    std::cout << "[Main] Final stats - Updates: " << orderbook_mgr.total_updates()
              << " | Arb opportunities: " << orderbook_mgr.arb_opportunities() << std::endl;

    http_global_cleanup();

    std::cout << "[Main] Shutdown complete." << std::endl;
    return 0;
}
