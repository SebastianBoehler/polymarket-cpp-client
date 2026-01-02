/**
 * C++ Arbitrage Test - Batch Order Placement
 *
 * Places YES and NO orders simultaneously using batch API for arb strategy.
 * Tests combined price < 1 opportunities on BTC 15m markets.
 *
 * Build: cmake --build build --target arb_test
 * Run: PRIVATE_KEY=0x... FUNDER_ADDRESS=0x... ./build/arb_test
 */

#include "order_signer.hpp"
#include "http_client.hpp"
#include "websocket_client.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
#include <cstdlib>
#include <chrono>
#include <iomanip>
#include <thread>
#include <atomic>
#include <mutex>

using json = nlohmann::json;
using namespace polymarket;

// Polymarket contract addresses (Polygon mainnet)
const std::string CLOB_API = "https://clob.polymarket.com";
const std::string NEG_RISK_CTF_EXCHANGE = "0xC5d563A36AE78145C45a50134d48A1215220f80a";
const std::string CTF_EXCHANGE = "0x4bFb41d5B3570DeFd03C39a9A4D8dE6Bd8B8982E";

struct MarketInfo
{
    std::string slug;
    std::string token_yes;
    std::string token_no;
    double best_ask_yes = 0.0;
    double best_ask_no = 0.0;
    bool is_neg_risk = false;
    std::string exchange_address;
    uint64_t expiry_ts = 0;
};

void print_usage()
{
    std::cout << "C++ Arbitrage Test - Batch Order Placement\n"
              << "==========================================\n\n"
              << "Environment variables:\n"
              << "  PRIVATE_KEY      - Wallet private key (required)\n"
              << "  FUNDER_ADDRESS   - Address holding funds (for proxy wallets)\n"
              << "  SIZE_USDC        - Size per leg in USDC (default: 1)\n"
              << "  TRIGGER_COMBINED - Trigger when sum < this (default: 0.995)\n"
              << "  DRY_RUN          - Set to 'false' for live orders (default: true)\n\n"
              << "Options:\n"
              << "  --help           - Show this help\n"
              << std::endl;
}

int main(int argc, char *argv[])
{
    for (int i = 1; i < argc; i++)
    {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h")
        {
            print_usage();
            return 0;
        }
    }

    // Get environment variables
    const char *private_key_env = std::getenv("PRIVATE_KEY");
    const char *funder_address_env = std::getenv("FUNDER_ADDRESS");
    const char *size_usdc_env = std::getenv("SIZE_USDC");
    const char *trigger_env = std::getenv("TRIGGER_COMBINED");
    const char *dry_run_env = std::getenv("DRY_RUN");

    if (!private_key_env)
    {
        std::cerr << "Error: PRIVATE_KEY environment variable required\n";
        print_usage();
        return 1;
    }

    std::string private_key = private_key_env;
    std::string funder_address = funder_address_env ? funder_address_env : "";
    double size_usdc = size_usdc_env ? std::stod(size_usdc_env) : 1.0;
    double trigger_combined = trigger_env ? std::stod(trigger_env) : 0.995;
    bool dry_run = dry_run_env ? std::string(dry_run_env) != "false" : true;

    std::cout << "=== C++ Arbitrage Test ===\n";
    std::cout << "Size per leg: $" << size_usdc << "\n";
    std::cout << "Trigger combined: " << trigger_combined << "\n";
    std::cout << "Dry run: " << (dry_run ? "true" : "false") << "\n\n";

    // Initialize signer
    OrderSigner signer(private_key);
    if (funder_address.empty())
    {
        funder_address = signer.address();
    }
    std::cout << "Signer: " << signer.address() << "\n";
    std::cout << "Funder: " << funder_address << "\n\n";

    http_global_init();
    HttpClient http;
    http.set_base_url(CLOB_API);
    http.set_timeout_ms(10000);

    // Derive API credentials
    std::cout << "[1] Deriving API credentials...\n";
    ApiCredentials creds;
    try
    {
        creds = signer.create_or_derive_api_credentials(http, funder_address);
        std::cout << "    API key: " << creds.api_key.substr(0, 8) << "...\n";
    }
    catch (const std::exception &e)
    {
        std::cerr << "    Failed to derive credentials: " << e.what() << "\n";
        http_global_cleanup();
        return 1;
    }

    // Find BTC 15m market with liquidity
    std::cout << "\n[2] Finding BTC 15m market with liquidity...\n";

    MarketInfo market;
    uint64_t now_ts = static_cast<uint64_t>(std::time(nullptr));
    uint64_t min_time_left = 2 * 60; // 2 minutes minimum

    // Try current and next few 15-minute windows
    std::vector<std::pair<uint64_t, uint64_t>> candidates;
    uint64_t current_window = (now_ts / 900) * 900;
    for (int i = 0; i <= 3; i++)
    {
        uint64_t start_ts = current_window + i * 900;
        uint64_t expiry_ts = start_ts + 900;
        if (expiry_ts > now_ts + min_time_left)
        {
            candidates.push_back({start_ts, expiry_ts});
        }
    }

    // Sort by expiry (soonest first)
    std::sort(candidates.begin(), candidates.end(),
              [](const auto &a, const auto &b)
              { return a.second < b.second; });

    for (const auto &[target_ts, expiry_ts] : candidates)
    {
        std::string slug = "btc-updown-15m-" + std::to_string(target_ts);
        uint64_t time_left = expiry_ts - now_ts;

        HttpClient gamma_http;
        gamma_http.set_base_url("https://gamma-api.polymarket.com");
        gamma_http.set_timeout_ms(10000);

        auto gamma_response = gamma_http.get("/events?slug=" + slug);
        if (!gamma_response.ok())
            continue;

        auto gamma_json = json::parse(gamma_response.body);
        if (!gamma_json.is_array() || gamma_json.empty())
            continue;

        auto &event = gamma_json[0];
        if (!event.contains("markets") || event["markets"].empty())
            continue;

        auto &mkt = event["markets"][0];
        auto token_ids = json::parse(mkt["clobTokenIds"].get<std::string>());
        std::string yes_token = token_ids[0].get<std::string>();
        std::string no_token = token_ids[1].get<std::string>();

        // Get orderbooks for both tokens
        auto yes_book = http.get("/book?token_id=" + yes_token);
        auto no_book = http.get("/book?token_id=" + no_token);

        if (!yes_book.ok() || !no_book.ok())
            continue;

        auto yes_json = json::parse(yes_book.body);
        auto no_json = json::parse(no_book.body);

        if (!yes_json.contains("asks") || yes_json["asks"].empty() ||
            !no_json.contains("asks") || no_json["asks"].empty())
        {
            std::cout << "    Skipping " << slug << " - no liquidity\n";
            continue;
        }

        double best_ask_yes = std::stod(yes_json["asks"][0]["price"].get<std::string>());
        double best_ask_no = std::stod(no_json["asks"][0]["price"].get<std::string>());

        if (best_ask_yes <= 0.0 || best_ask_yes >= 1.0 ||
            best_ask_no <= 0.0 || best_ask_no >= 1.0)
            continue;

        // Check neg_risk
        bool is_neg_risk = false;
        auto neg_risk_response = http.get("/neg-risk?token_id=" + yes_token);
        if (neg_risk_response.ok())
        {
            auto neg_risk_json = json::parse(neg_risk_response.body);
            is_neg_risk = neg_risk_json.value("neg_risk", false);
        }

        market.slug = slug;
        market.token_yes = yes_token;
        market.token_no = no_token;
        market.best_ask_yes = best_ask_yes;
        market.best_ask_no = best_ask_no;
        market.is_neg_risk = is_neg_risk;
        market.exchange_address = is_neg_risk ? NEG_RISK_CTF_EXCHANGE : CTF_EXCHANGE;
        market.expiry_ts = expiry_ts;

        std::cout << "    Found: " << slug << " (expires in " << time_left / 60 << "min)\n";
        std::cout << "    YES token: " << yes_token.substr(0, 30) << "...\n";
        std::cout << "    NO token:  " << no_token.substr(0, 30) << "...\n";
        std::cout << "    Best ask YES: " << best_ask_yes << "\n";
        std::cout << "    Best ask NO:  " << best_ask_no << "\n";
        std::cout << "    Combined:     " << (best_ask_yes + best_ask_no) << "\n";
        std::cout << "    neg_risk:     " << (is_neg_risk ? "true" : "false") << "\n";
        break;
    }

    if (market.token_yes.empty())
    {
        std::cerr << "    Could not find active BTC 15m market with liquidity\n";
        http_global_cleanup();
        return 1;
    }

    // Monitor orderbook via WebSocket for real-time updates
    std::cout << "\n[3] Connecting to WebSocket for real-time orderbook...\n";

    std::atomic<double> ws_best_ask_yes{market.best_ask_yes};
    std::atomic<double> ws_best_ask_no{market.best_ask_no};
    std::atomic<bool> opportunity_found{false};
    std::atomic<bool> ws_connected{false};
    std::mutex price_mutex;

    WebSocketClient ws;
    ws.set_url("wss://ws-subscriptions-clob.polymarket.com/ws/market");
    ws.set_auto_reconnect(true);
    ws.set_ping_interval_ms(10000);

    ws.on_connect([&]()
                  {
        std::cout << "    WebSocket connected!\n";
        ws_connected.store(true);

        // Subscribe to orderbook updates for both tokens
        json subscribe_msg;
        subscribe_msg["type"] = "subscribe";
        subscribe_msg["channel"] = "market";
        subscribe_msg["assets_ids"] = json::array({market.token_yes, market.token_no});
        ws.send(subscribe_msg.dump());
        std::cout << "    Subscribed to orderbook updates\n"; });

    ws.on_message([&](const std::string &msg)
                  {
        try {
            auto j = json::parse(msg);
            
            // Handle orderbook updates
            if (j.contains("event_type") && j["event_type"] == "book") {
                std::string asset_id = j.value("asset_id", "");
                
                if (j.contains("bids") || j.contains("asks")) {
                    std::lock_guard<std::mutex> lock(price_mutex);
                    
                    if (asset_id == market.token_yes && j.contains("asks") && !j["asks"].empty()) {
                        // Find best (lowest) ask
                        double best = 1.0;
                        for (const auto& ask : j["asks"]) {
                            double price = std::stod(ask["price"].get<std::string>());
                            if (price < best) best = price;
                        }
                        ws_best_ask_yes.store(best);
                    } else if (asset_id == market.token_no && j.contains("asks") && !j["asks"].empty()) {
                        double best = 1.0;
                        for (const auto& ask : j["asks"]) {
                            double price = std::stod(ask["price"].get<std::string>());
                            if (price < best) best = price;
                        }
                        ws_best_ask_no.store(best);
                    }
                }
            }
            
            // Also handle price updates
            if (j.contains("event_type") && j["event_type"] == "price_change") {
                std::string asset_id = j.value("asset_id", "");
                if (j.contains("price")) {
                    double price = std::stod(j["price"].get<std::string>());
                    std::lock_guard<std::mutex> lock(price_mutex);
                    if (asset_id == market.token_yes) {
                        ws_best_ask_yes.store(price);
                    } else if (asset_id == market.token_no) {
                        ws_best_ask_no.store(price);
                    }
                }
            }
        } catch (...) {
            // Ignore parse errors
        } });

    ws.on_error([](const std::string &err)
                { std::cerr << "    WebSocket error: " << err << "\n"; });

    // Start WebSocket in background thread
    std::thread ws_thread([&ws]()
                          {
        ws.connect();
        ws.run(); });

    // Wait for connection
    for (int i = 0; i < 50 && !ws_connected.load(); i++)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (!ws_connected.load())
    {
        std::cerr << "    Failed to connect to WebSocket, falling back to REST polling\n";
    }

    std::cout << "\n[4] Monitoring for arbitrage opportunity (combined < " << trigger_combined << ")...\n";
    std::cout << "    Press Ctrl+C to exit\n\n";

    double combined = ws_best_ask_yes.load() + ws_best_ask_no.load();
    int poll_count = 0;
    const int max_seconds = 600; // Max 10 minutes

    while (combined >= trigger_combined && poll_count < max_seconds)
    {
        // Check if market is about to expire
        uint64_t now = static_cast<uint64_t>(std::time(nullptr));
        int64_t time_left = static_cast<int64_t>(market.expiry_ts) - static_cast<int64_t>(now);

        if (time_left <= 30)
        {
            std::cout << "\n\n    Market expiring, switching to next market...\n";
            ws.stop();
            ws_thread.join();

            // Find next market
            market = MarketInfo{}; // Reset
            uint64_t new_now_ts = static_cast<uint64_t>(std::time(nullptr));
            uint64_t new_window = (new_now_ts / 900) * 900;

            for (int i = 0; i <= 3; i++)
            {
                uint64_t start_ts = new_window + i * 900;
                uint64_t exp_ts = start_ts + 900;
                if (exp_ts <= new_now_ts + 120)
                    continue; // Need at least 2 min

                std::string slug = "btc-updown-15m-" + std::to_string(start_ts);

                HttpClient gamma_http;
                gamma_http.set_base_url("https://gamma-api.polymarket.com");
                gamma_http.set_timeout_ms(10000);

                auto gamma_response = gamma_http.get("/events?slug=" + slug);
                if (!gamma_response.ok())
                    continue;

                auto gamma_json = json::parse(gamma_response.body);
                if (!gamma_json.is_array() || gamma_json.empty())
                    continue;

                auto &event = gamma_json[0];
                if (!event.contains("markets") || event["markets"].empty())
                    continue;

                auto &mkt = event["markets"][0];
                auto token_ids = json::parse(mkt["clobTokenIds"].get<std::string>());

                market.slug = slug;
                market.token_yes = token_ids[0].get<std::string>();
                market.token_no = token_ids[1].get<std::string>();
                market.expiry_ts = exp_ts;

                // Check neg_risk
                auto neg_risk_response = http.get("/neg-risk?token_id=" + market.token_yes);
                if (neg_risk_response.ok())
                {
                    auto neg_risk_json = json::parse(neg_risk_response.body);
                    market.is_neg_risk = neg_risk_json.value("neg_risk", false);
                }
                market.exchange_address = market.is_neg_risk ? NEG_RISK_CTF_EXCHANGE : CTF_EXCHANGE;

                std::cout << "    Switched to: " << slug << " (expires in " << (exp_ts - new_now_ts) / 60 << "min)\n";
                break;
            }

            if (market.token_yes.empty())
            {
                std::cerr << "    Could not find new market\n";
                http_global_cleanup();
                return 1;
            }

            // Reconnect WebSocket with new tokens
            ws_best_ask_yes.store(0.5);
            ws_best_ask_no.store(0.5);
            ws_connected.store(false);

            ws.set_url("wss://ws-subscriptions-clob.polymarket.com/ws/market");
            ws_thread = std::thread([&ws]()
                                    {
                ws.connect();
                ws.run(); });

            // Wait for reconnection
            for (int i = 0; i < 50 && !ws_connected.load(); i++)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }

            std::cout << "\n";
            continue;
        }

        // Get latest prices from WebSocket or fallback to REST
        double yes_price = ws_best_ask_yes.load();
        double no_price = ws_best_ask_no.load();

        // Fallback: fetch via REST if WebSocket prices seem stale (both at 0.99)
        if (yes_price >= 0.99 && no_price >= 0.99 && poll_count % 5 == 0)
        {
            auto yes_book = http.get("/book?token_id=" + market.token_yes);
            auto no_book = http.get("/book?token_id=" + market.token_no);
            if (yes_book.ok() && no_book.ok())
            {
                auto yes_json = json::parse(yes_book.body);
                auto no_json = json::parse(no_book.body);
                if (yes_json.contains("asks") && !yes_json["asks"].empty())
                {
                    yes_price = std::stod(yes_json["asks"][0]["price"].get<std::string>());
                    ws_best_ask_yes.store(yes_price);
                }
                if (no_json.contains("asks") && !no_json["asks"].empty())
                {
                    no_price = std::stod(no_json["asks"][0]["price"].get<std::string>());
                    ws_best_ask_no.store(no_price);
                }
            }
        }

        combined = yes_price + no_price;
        market.best_ask_yes = yes_price;
        market.best_ask_no = no_price;

        // Format countdown timer
        int mins = time_left / 60;
        int secs = time_left % 60;

        // Print status with countdown
        std::cout << "\r    [" << mins << ":" << std::setfill('0') << std::setw(2) << secs << std::setfill(' ')
                  << "] UP: " << std::fixed << std::setprecision(2) << yes_price
                  << " + DOWN: " << no_price
                  << " = " << std::setprecision(4) << combined
                  << " (trigger: " << trigger_combined << ")    " << std::flush;

        poll_count++;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    // Stop WebSocket
    ws.stop();
    ws_thread.join();

    if (poll_count >= max_seconds)
    {
        std::cout << "\n    Timeout waiting for opportunity\n";
        http_global_cleanup();
        return 1;
    }

    std::cout << "\n\n    ✅ OPPORTUNITY FOUND!\n";
    std::cout << "    Combined: " << combined << " < " << trigger_combined << "\n";
    std::cout << "    Potential profit: " << std::fixed << std::setprecision(4) << (1.0 - combined) * 100 << "%\n";

    // Calculate order amounts
    double slippage = 0.01; // 1 cent slippage buffer
    double yes_price = std::floor((market.best_ask_yes + slippage) * 100) / 100;
    double no_price = std::floor((market.best_ask_no + slippage) * 100) / 100;

    // Cap at 0.99
    yes_price = std::min(yes_price, 0.99);
    no_price = std::min(no_price, 0.99);

    double maker_amount = std::floor(size_usdc * 100) / 100; // Round to 2 decimals

    // Calculate taker amounts (shares) - match TS client rounding
    double yes_taker_raw = maker_amount / yes_price;
    double no_taker_raw = maker_amount / no_price;

    // TS rounding: roundUp to 8 decimals, then roundDown to 4 decimals
    yes_taker_raw = std::ceil(yes_taker_raw * 100000000) / 100000000;
    yes_taker_raw = std::floor(yes_taker_raw * 10000) / 10000;
    no_taker_raw = std::ceil(no_taker_raw * 100000000) / 100000000;
    no_taker_raw = std::floor(no_taker_raw * 10000) / 10000;

    std::cout << "\n[4] Order details:\n";
    std::cout << "    YES: $" << maker_amount << " @ " << yes_price << " = " << yes_taker_raw << " shares\n";
    std::cout << "    NO:  $" << maker_amount << " @ " << no_price << " = " << no_taker_raw << " shares\n";
    std::cout << "    Combined with slippage: " << (yes_price + no_price) << "\n";

    if (dry_run)
    {
        std::cout << "\n[DRY RUN] Would place batch order - set DRY_RUN=false to execute\n";
        http_global_cleanup();
        return 0;
    }

    // Create both orders
    std::cout << "\n[5] Creating and signing orders...\n";
    auto sign_start = std::chrono::high_resolution_clock::now();

    OrderData yes_order;
    yes_order.maker = funder_address;
    yes_order.taker = "0x0000000000000000000000000000000000000000";
    yes_order.token_id = market.token_yes;
    yes_order.maker_amount = to_wei(maker_amount, 6);
    yes_order.taker_amount = to_wei(yes_taker_raw, 6);
    yes_order.side = OrderSide::BUY;
    yes_order.fee_rate_bps = "0";
    yes_order.nonce = "0";
    yes_order.signer = signer.address();
    yes_order.expiration = "0";
    yes_order.signature_type = (funder_address != signer.address())
                                   ? SignatureType::POLY_GNOSIS_SAFE
                                   : SignatureType::EOA;

    OrderData no_order;
    no_order.maker = funder_address;
    no_order.taker = "0x0000000000000000000000000000000000000000";
    no_order.token_id = market.token_no;
    no_order.maker_amount = to_wei(maker_amount, 6);
    no_order.taker_amount = to_wei(no_taker_raw, 6);
    no_order.side = OrderSide::BUY;
    no_order.fee_rate_bps = "0";
    no_order.nonce = "0";
    no_order.signer = signer.address();
    no_order.expiration = "0";
    no_order.signature_type = yes_order.signature_type;

    auto yes_signed = signer.sign_order(yes_order, market.exchange_address);
    auto no_signed = signer.sign_order(no_order, market.exchange_address);

    auto sign_end = std::chrono::high_resolution_clock::now();
    auto sign_ms = std::chrono::duration_cast<std::chrono::milliseconds>(sign_end - sign_start).count();
    std::cout << "    Orders signed in " << sign_ms << "ms\n";

    // Build batch order payload
    std::cout << "\n[6] Posting batch order...\n";

    nlohmann::ordered_json yes_payload;
    {
        nlohmann::ordered_json order_obj;
        order_obj["salt"] = std::stoll(yes_signed.salt);
        order_obj["maker"] = yes_signed.maker;
        order_obj["signer"] = yes_signed.signer;
        order_obj["taker"] = yes_signed.taker;
        order_obj["tokenId"] = yes_signed.token_id;
        order_obj["makerAmount"] = yes_signed.maker_amount;
        order_obj["takerAmount"] = yes_signed.taker_amount;
        order_obj["side"] = "BUY";
        order_obj["expiration"] = yes_signed.expiration;
        order_obj["nonce"] = yes_signed.nonce;
        order_obj["feeRateBps"] = yes_signed.fee_rate_bps;
        order_obj["signatureType"] = static_cast<int>(yes_signed.signature_type);
        order_obj["signature"] = yes_signed.signature;
        yes_payload["deferExec"] = false;
        yes_payload["order"] = order_obj;
        yes_payload["owner"] = creds.api_key;
        yes_payload["orderType"] = "FOK"; // Fill-Or-Kill for immediate execution
    }

    nlohmann::ordered_json no_payload;
    {
        nlohmann::ordered_json order_obj;
        order_obj["salt"] = std::stoll(no_signed.salt);
        order_obj["maker"] = no_signed.maker;
        order_obj["signer"] = no_signed.signer;
        order_obj["taker"] = no_signed.taker;
        order_obj["tokenId"] = no_signed.token_id;
        order_obj["makerAmount"] = no_signed.maker_amount;
        order_obj["takerAmount"] = no_signed.taker_amount;
        order_obj["side"] = "BUY";
        order_obj["expiration"] = no_signed.expiration;
        order_obj["nonce"] = no_signed.nonce;
        order_obj["feeRateBps"] = no_signed.fee_rate_bps;
        order_obj["signatureType"] = static_cast<int>(no_signed.signature_type);
        order_obj["signature"] = no_signed.signature;
        no_payload["deferExec"] = false;
        no_payload["order"] = order_obj;
        no_payload["owner"] = creds.api_key;
        no_payload["orderType"] = "FOK";
    }

    // Batch array - convert ordered_json to regular json for array
    nlohmann::json batch_payload = nlohmann::json::array();
    batch_payload.push_back(nlohmann::json::parse(yes_payload.dump()));
    batch_payload.push_back(nlohmann::json::parse(no_payload.dump()));

    std::string body_str = batch_payload.dump();
    std::cout << "    Batch payload size: " << body_str.size() << " bytes\n";

    // Generate L2 headers for batch endpoint
    auto l2 = signer.generate_l2_headers(creds, "POST", "/orders", body_str, funder_address);

    std::map<std::string, std::string> headers;
    headers["Content-Type"] = "application/json";
    headers["POLY_ADDRESS"] = l2.poly_address;
    headers["POLY_SIGNATURE"] = l2.poly_signature;
    headers["POLY_TIMESTAMP"] = l2.poly_timestamp;
    headers["POLY_API_KEY"] = l2.poly_api_key;
    headers["POLY_PASSPHRASE"] = l2.poly_passphrase;

    auto post_start = std::chrono::high_resolution_clock::now();
    auto response = http.post("/orders", body_str, headers);
    auto post_end = std::chrono::high_resolution_clock::now();
    auto post_ms = std::chrono::duration_cast<std::chrono::milliseconds>(post_end - post_start).count();

    std::cout << "\n[7] Results:\n";
    std::cout << "    Sign latency: " << sign_ms << "ms\n";
    std::cout << "    Post latency: " << post_ms << "ms\n";
    std::cout << "    Total:        " << (sign_ms + post_ms) << "ms\n";
    std::cout << "    HTTP status:  " << response.status_code << "\n";

    if (response.ok())
    {
        auto results = json::parse(response.body);
        std::cout << "\n    Response:\n";

        bool yes_filled = false, no_filled = false;
        double yes_shares = 0, no_shares = 0;
        double yes_cost = 0, no_cost = 0;

        if (results.is_array() && results.size() >= 2)
        {
            auto &yes_result = results[0];
            auto &no_result = results[1];

            yes_filled = yes_result.value("success", false) &&
                         yes_result.value("errorMsg", "") == "" &&
                         yes_result.contains("orderID");
            no_filled = no_result.value("success", false) &&
                        no_result.value("errorMsg", "") == "" &&
                        no_result.contains("orderID");

            if (yes_filled)
            {
                yes_shares = std::stod(yes_result.value("takingAmount", "0"));
                yes_cost = std::stod(yes_result.value("makingAmount", "0"));
                std::cout << "    YES: ✅ FILLED - " << yes_shares << " shares for $" << yes_cost << "\n";
                std::cout << "         OrderID: " << yes_result["orderID"].get<std::string>() << "\n";
            }
            else
            {
                std::cout << "    YES: ❌ NOT FILLED - " << yes_result.value("errorMsg", "unknown error") << "\n";
            }

            if (no_filled)
            {
                no_shares = std::stod(no_result.value("takingAmount", "0"));
                no_cost = std::stod(no_result.value("makingAmount", "0"));
                std::cout << "    NO:  ✅ FILLED - " << no_shares << " shares for $" << no_cost << "\n";
                std::cout << "         OrderID: " << no_result["orderID"].get<std::string>() << "\n";
            }
            else
            {
                std::cout << "    NO:  ❌ NOT FILLED - " << no_result.value("errorMsg", "unknown error") << "\n";
            }

            if (yes_filled && no_filled)
            {
                double total_cost = yes_cost + no_cost;
                double min_shares = std::min(yes_shares, no_shares);
                double guaranteed_payout = min_shares; // Each pair pays $1
                double profit = guaranteed_payout - total_cost;
                std::cout << "\n    === ARBITRAGE RESULT ===\n";
                std::cout << "    Total cost:        $" << std::fixed << std::setprecision(6) << total_cost << "\n";
                std::cout << "    Min shares (pair): " << min_shares << "\n";
                std::cout << "    Guaranteed payout: $" << guaranteed_payout << "\n";
                std::cout << "    Profit:            $" << profit << " (" << (profit / total_cost * 100) << "%)\n";
            }
            else if (yes_filled || no_filled)
            {
                std::cout << "\n    ⚠️  PARTIAL FILL - One side filled, other didn't!\n";
                std::cout << "    Consider selling the filled position to exit.\n";
            }
        }
        else
        {
            std::cout << "    Unexpected response format: " << response.body << "\n";
        }
    }
    else
    {
        std::cout << "    Error: " << response.body << "\n";
    }

    http_global_cleanup();
    return 0;
}
