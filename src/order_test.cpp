/**
 * Order signing test script for Polymarket CLOB API.
 *
 * Tests:
 * 1. Private key to address derivation
 * 2. EIP-712 order signing
 * 3. API credential generation
 * 4. Order placement (dry-run by default)
 *
 * Usage:
 *   PRIVATE_KEY=0x... FUNDER_ADDRESS=0x... ./order_test [--live]
 */

#include "order_signer.hpp"
#include "clob_client.hpp"
#include "http_client.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <ctime>
#include <iostream>
#include <cstdlib>
#include <string>
#include <vector>

using json = nlohmann::json;
using namespace polymarket;

// Polymarket contract addresses (Polygon mainnet)
const std::string CLOB_API = "https://clob.polymarket.com";
const std::string NEG_RISK_CTF_EXCHANGE = "0xe2222d279d744050d28e00520010520000310F59";
const std::string CTF_EXCHANGE = "0xE111180000d2663C0091e4f400237545B87B996B";

struct LiveMarketData
{
    std::string token_id;
    std::string slug;
    double best_ask = 0.0;
    std::string tick_size;
    bool neg_risk = false;
};

void print_usage()
{
    std::cout << "Order Signing Test for Polymarket\n"
              << "==================================\n\n"
              << "Environment variables:\n"
              << "  PRIVATE_KEY      - Wallet private key (required)\n"
              << "  FUNDER_ADDRESS   - Address holding funds (for proxy wallets)\n"
              << "  API_KEY          - Polymarket API key\n"
              << "  API_SECRET       - Polymarket API secret\n"
              << "  API_PASSPHRASE   - Polymarket API passphrase\n\n"
              << "Options:\n"
              << "  --live           - Actually place orders (default: dry-run)\n"
              << "  --help           - Show this help\n"
              << std::endl;
}

int main(int argc, char *argv[])
{
    bool live_mode = false;

    for (int i = 1; i < argc; i++)
    {
        std::string arg = argv[i];
        if (arg == "--help")
        {
            print_usage();
            return 0;
        }
        else if (arg == "--live")
        {
            live_mode = true;
        }
    }

    // Get environment variables
    const char *private_key_env = std::getenv("PRIVATE_KEY");
    const char *funder_address_env = std::getenv("FUNDER_ADDRESS");
    const char *api_key_env = std::getenv("API_KEY");
    const char *api_secret_env = std::getenv("API_SECRET");
    const char *api_passphrase_env = std::getenv("API_PASSPHRASE");

    if (!private_key_env)
    {
        std::cerr << "Error: PRIVATE_KEY environment variable required\n";
        print_usage();
        return 1;
    }

    std::string private_key = private_key_env;
    std::string funder_address = funder_address_env ? funder_address_env : "";

    std::cout << "╔══════════════════════════════════════════════════════════════╗\n"
              << "║           Polymarket Order Signing Test                      ║\n"
              << "╚══════════════════════════════════════════════════════════════╝\n\n";

    std::cout << "Mode: " << (live_mode ? "LIVE (orders will be placed!)" : "DRY-RUN") << "\n\n";

    try
    {
        // Initialize signer
        std::cout << "[1] Initializing order signer...\n";
        OrderSigner signer(private_key, 137); // Polygon mainnet

        std::cout << "    Derived address: " << signer.address() << "\n";

        if (funder_address.empty())
        {
            funder_address = signer.address();
        }
        std::cout << "    Funder address:  " << funder_address << "\n\n";

        // Test signing a sample order
        std::cout << "[2] Testing order signing...\n";

        // Sample order data - MUST match TypeScript test exactly
        OrderData order;
        order.maker = signer.address(); // Use derived address
        order.taker = "0x0000000000000000000000000000000000000000";
        order.token_id = "1234567890";
        order.maker_amount = "5000000";  // 5 USDC (6 decimals)
        order.taker_amount = "10000000"; // 10 shares
        order.side = OrderSide::BUY;
        order.signer = signer.address();
        order.expiration = "0";
        order.signature_type = SignatureType::EOA;

        // Use neg_risk exchange for crypto markets
        auto signed_order = signer.sign_order(order, NEG_RISK_CTF_EXCHANGE);

        // Also test with FIXED parameters for comparison with TypeScript
        std::cout << "\n[2b] Testing with FIXED params for TypeScript comparison...\n";

        // Use simple params to verify signing works
        OrderData fixed_order;
        fixed_order.maker = funder_address;
        fixed_order.signer = signer.address();
        fixed_order.taker = "0x0000000000000000000000000000000000000000";
        fixed_order.token_id = "1234567890";
        fixed_order.maker_amount = "1000000";
        fixed_order.taker_amount = "2000000";
        fixed_order.side = OrderSide::BUY;
        fixed_order.expiration = "0";
        fixed_order.signature_type = SignatureType::POLY_PROXY;
        fixed_order.timestamp = "1713398400000";

        std::string fixed_salt = "123456789";
        auto signed_order_fixed = signer.sign_order_with_salt(fixed_order, NEG_RISK_CTF_EXCHANGE, fixed_salt);
        std::cout << "    Fixed salt: " << fixed_salt << "\n";
        std::cout << "    C++ Signature: " << signed_order_fixed.signature << "\n";
        std::cout << "    Expected (official V2 SDK): 0x172933dc26efdf531dc959a95743b5c13147c5027a1eaa172701f1e599a130851f8126ce1c4fa043e360e4ee30211f49151d6f192a32b1cbad2123463e3d45641c\n";

        if (signed_order_fixed.signature == "0x172933dc26efdf531dc959a95743b5c13147c5027a1eaa172701f1e599a130851f8126ce1c4fa043e360e4ee30211f49151d6f192a32b1cbad2123463e3d45641c")
        {
            std::cout << "    ✅ SIGNATURES MATCH!\n";
        }
        else
        {
            std::cout << "    ❌ SIGNATURES DO NOT MATCH\n";
        }

        std::cout << "    Order signed successfully!\n";
        std::cout << "    Salt:      " << signed_order.salt.substr(0, 16) << "...\n";
        std::cout << "    Signature: " << signed_order.signature.substr(0, 20) << "...\n\n";

        // Build order JSON
        json order_json;
        order_json["salt"] = signed_order.salt;
        order_json["maker"] = signed_order.maker;
        order_json["signer"] = signed_order.signer;
        order_json["taker"] = signed_order.taker;
        order_json["tokenId"] = signed_order.token_id;
        order_json["makerAmount"] = signed_order.maker_amount;
        order_json["takerAmount"] = signed_order.taker_amount;
        order_json["expiration"] = signed_order.expiration;
        order_json["side"] = signed_order.side;
        order_json["signatureType"] = signed_order.signature_type;
        order_json["timestamp"] = signed_order.timestamp;
        order_json["metadata"] = signed_order.metadata;
        order_json["builder"] = signed_order.builder;
        order_json["signature"] = signed_order.signature;

        std::cout << "[3] Order JSON:\n";
        std::cout << order_json.dump(2) << "\n\n";

        // Test API connectivity
        std::cout << "[4] Testing API connectivity...\n";

        http_global_init();
        HttpClient http;
        http.set_base_url(CLOB_API);
        http.set_timeout_ms(5000);

        auto response = http.get("/");
        if (response.ok())
        {
            std::cout << "    API reachable: OK\n";
        }
        else
        {
            std::cout << "    API reachable: FAILED (" << response.status_code << ")\n";
        }

        // Try to derive API credentials (may fail for proxy wallets - that's OK)
        ApiCredentials creds;
        bool have_creds = false;
        if (api_key_env && api_secret_env && api_passphrase_env)
        {
            std::cout << "\n[5] Using provided API credentials...\n";
            creds.api_key = api_key_env;
            creds.api_secret = api_secret_env;
            creds.api_passphrase = api_passphrase_env;
            have_creds = true;
        }
        else
        {
            std::cout << "\n[5] Attempting to derive API credentials (L1 auth) for funder: " << funder_address << "\n";
            try
            {
                // Pass funder_address so API key is associated with the correct address
                creds = signer.create_or_derive_api_credentials(http, funder_address);
                std::cout << "    API key derived: " << creds.api_key.substr(0, 8) << "...\n";
                have_creds = true;
            }
            catch (const std::exception &e)
            {
                std::cout << "    Could not derive API credentials: " << e.what() << "\n";
                std::cout << "    Will proceed with order signing only...\n";
            }
        }

        if (have_creds)
        {
            std::cout << "    API Secret (first 20): " << creds.api_secret.substr(0, 20) << "\n";
            std::cout << "    API Passphrase: " << creds.api_passphrase << "\n";

            // Test fetching open orders (requires L2 auth)
            std::cout << "\n[6] Testing authenticated API call (GET /data/orders)...\n";

            // Generate L2 headers for the actual endpoint we're calling
            auto headers = signer.generate_l2_headers(creds, "GET", "/data/orders", "", funder_address);
            std::cout << "    POLY_ADDRESS: " << headers.poly_address << "\n";
            std::cout << "    POLY_SIGNATURE: " << headers.poly_signature.substr(0, 30) << "...\n";

            HttpClient auth_http;
            auth_http.set_base_url(CLOB_API);
            auth_http.set_timeout_ms(10000);

            std::map<std::string, std::string> auth_headers;
            auth_headers["POLY_ADDRESS"] = headers.poly_address;
            auth_headers["POLY_SIGNATURE"] = headers.poly_signature;
            auth_headers["POLY_TIMESTAMP"] = headers.poly_timestamp;
            auth_headers["POLY_API_KEY"] = headers.poly_api_key;
            auth_headers["POLY_PASSPHRASE"] = headers.poly_passphrase;

            auto orders_response = auth_http.get("/data/orders", auth_headers);
            if (orders_response.ok())
            {
                std::cout << "    Open orders fetch: OK\n";
                auto orders_json = json::parse(orders_response.body);
                std::cout << "    Found " << orders_json.size() << " open orders\n";
            }
            else
            {
                std::cout << "    Open orders fetch: FAILED (" << orders_response.status_code << ")\n";
            }
        }

        if (live_mode)
        {
            std::cout << "\n[7] LIVE MODE - Placing $1 test order on BTC market...\n";

            // Get a real token ID from a 15m market (like arb-smoke.ts)
            std::cout << "    Fetching nearest active BTC 15m market...\n";

            // Find markets with at least 2 min left before expiry
            LiveMarketData live_market;
            uint64_t now_ts = static_cast<uint64_t>(std::time(nullptr));
            uint64_t min_time_left = 2 * 60; // 2 minutes minimum
            ClobClient market_data_client(CLOB_API, 137);
            market_data_client.set_user_agent("polymarket-cpp-client/order-test");

            // Try current and next few 15-minute windows
            std::vector<std::pair<uint64_t, uint64_t>> candidates; // (start_ts, expiry_ts)
            uint64_t current_window = (now_ts / 900) * 900;
            for (int i = 0; i <= 3; i++)
            {
                uint64_t start_ts = current_window + i * 900;
                uint64_t expiry_ts = start_ts + 900; // 15 min after start
                // Only consider if expiry > now + min_time_left
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
                gamma_http.set_user_agent("polymarket-cpp-client/order-test");

                auto gamma_response = gamma_http.get("/events?slug=" + slug);

                if (gamma_response.ok())
                {
                    auto gamma_json = json::parse(gamma_response.body);
                    if (gamma_json.is_array() && !gamma_json.empty())
                    {
                        auto &event = gamma_json[0];
                        if (event.contains("markets") && !event["markets"].empty())
                        {
                            auto &market = event["markets"][0];
                            auto token_ids = json::parse(market["clobTokenIds"].get<std::string>());
                            std::string candidate_token = token_ids[0].get<std::string>();

                            auto book = market_data_client.get_order_book(candidate_token);
                            if (!book || book->asks.empty())
                            {
                                std::cout << "    Skipping " << slug << " - no ask liquidity\n";
                                continue;
                            }

                            const double candidate_best_ask = book->best_ask();
                            if (candidate_best_ask <= 0.0 || candidate_best_ask >= 1.0)
                            {
                                std::cout << "    Skipping " << slug << " - invalid best ask " << candidate_best_ask << "\n";
                                continue;
                            }

                            auto tick_size = market_data_client.get_tick_size(candidate_token);
                            if (!tick_size || tick_size->minimum_tick_size.empty())
                            {
                                std::cout << "    Skipping " << slug << " - could not fetch tick size\n";
                                continue;
                            }

                            auto neg_risk = market_data_client.get_neg_risk(candidate_token);
                            if (!neg_risk)
                            {
                                std::cout << "    Skipping " << slug << " - could not fetch neg_risk\n";
                                continue;
                            }

                            live_market.token_id = candidate_token;
                            live_market.slug = slug;
                            live_market.best_ask = candidate_best_ask;
                            live_market.tick_size = tick_size->minimum_tick_size;
                            live_market.neg_risk = neg_risk->neg_risk;

                            std::cout << "    Found market with liquidity: " << slug << " (expires in " << time_left / 60 << "min)\n";
                            std::cout << "    Best ask: " << live_market.best_ask << "\n";
                            std::cout << "    Tick size: " << live_market.tick_size << "\n";
                            std::cout << "    neg_risk: " << (live_market.neg_risk ? "true" : "false") << "\n";
                            break;
                        }
                    }
                }
            }

            if (live_market.token_id.empty() || live_market.best_ask <= 0.0 || live_market.tick_size.empty())
            {
                std::cerr << "    Could not find active BTC 15m market with complete trading metadata\n";
                http_global_cleanup();
                return 1;
            }

            std::cout << "    YES token: " << live_market.token_id.substr(0, 30) << "...\n";

            // Use cached market metadata from discovery for order construction.
            std::string exchange_address = live_market.neg_risk ? NEG_RISK_CTF_EXCHANGE : CTF_EXCHANGE;
            std::cout << "    Exchange: " << exchange_address << "\n";

            if (!have_creds)
            {
                std::cerr << "    Live mode requires API credentials\n";
                http_global_cleanup();
                return 1;
            }

            const double order_usd = 1.0;
            const SignatureType live_signature_type = (funder_address != signer.address())
                                                          ? SignatureType::POLY_GNOSIS_SAFE
                                                          : SignatureType::EOA;
            ClobClient order_client(CLOB_API, 137, private_key, creds, live_signature_type, funder_address);

            CreateMarketOrderParams market_order;
            market_order.token_id = live_market.token_id;
            market_order.amount = order_usd;
            market_order.side = OrderSide::BUY;
            market_order.price = live_market.best_ask;
            market_order.tick_size = live_market.tick_size;
            market_order.neg_risk = live_market.neg_risk;

            std::cout << "    Placing FAK market order: $" << order_usd << "\n";

            auto real_signed = order_client.create_market_order(market_order);

            // Debug: print order data before signing
            std::cout << "    Order data for signing:\n";
            std::cout << "      maker: " << real_signed.maker << "\n";
            std::cout << "      signer: " << real_signed.signer << "\n";
            std::cout << "      taker: " << real_signed.taker << "\n";
            std::cout << "      tokenId: " << real_signed.token_id << "\n";
            std::cout << "      makerAmount: " << real_signed.maker_amount << "\n";
            std::cout << "      takerAmount: " << real_signed.taker_amount << "\n";
            std::cout << "      side: " << static_cast<int>(real_signed.side) << "\n";
            std::cout << "      signatureType: " << static_cast<int>(real_signed.signature_type) << "\n";
            std::cout << "      exchange: " << exchange_address << "\n";

            auto post_response = order_client.post_order(real_signed, OrderType::FAK);
            if (post_response.success)
            {
                std::cout << "\n    ✅ ORDER PLACED SUCCESSFULLY!\n";
                std::cout << "    Order ID: " << post_response.order_id << "\n";
                std::cout << "    Status: " << post_response.status << "\n";
                std::cout << "    Cost: $" << post_response.making_amount << "\n";
                std::cout << "    Shares: " << post_response.taking_amount << "\n";
            }
            else
            {
                std::cerr << "\n    Order placement failed: " << post_response.error_msg << "\n";
            }
        }

        http_global_cleanup();

        std::cout << "\n✅ Order signing test completed successfully!\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "\n❌ Error: " << e.what() << "\n";
        return 1;
    }
}
