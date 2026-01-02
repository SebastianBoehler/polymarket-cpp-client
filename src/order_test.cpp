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
#include "http_client.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
#include <cstdlib>

using json = nlohmann::json;
using namespace polymarket;

// Polymarket contract addresses (Polygon mainnet)
const std::string CLOB_API = "https://clob.polymarket.com";
const std::string NEG_RISK_CTF_EXCHANGE = "0xC5d563A36AE78145C45a50134d48A1215220f80a";
const std::string CTF_EXCHANGE = "0x4bFb41d5B3570DeFd03C39a9A4D8dE6Bd8B8982E";

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
        order.fee_rate_bps = "0";
        order.nonce = "0";
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
        fixed_order.fee_rate_bps = "0";
        fixed_order.nonce = "0";
        fixed_order.expiration = "0";
        fixed_order.signature_type = SignatureType::POLY_GNOSIS_SAFE;

        std::string fixed_salt = "123456789";
        auto signed_order_fixed = signer.sign_order_with_salt(fixed_order, NEG_RISK_CTF_EXCHANGE, fixed_salt);
        std::cout << "    Fixed salt: " << fixed_salt << "\n";
        std::cout << "    C++ Signature: " << signed_order_fixed.signature << "\n";
        std::cout << "    Expected (TS): 0x7883a3b2be0a2ec3ad8574fdf5fafe68a7d841369e2154272cbc9f8e66fc98bd27a7e89f0d51138be6b2f7b81012a2d4f475e2959f0a7ddf2ba0f5d756f6ae2f1c\n";

        if (signed_order_fixed.signature == "0x7883a3b2be0a2ec3ad8574fdf5fafe68a7d841369e2154272cbc9f8e66fc98bd27a7e89f0d51138be6b2f7b81012a2d4f475e2959f0a7ddf2ba0f5d756f6ae2f1c")
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
        order_json["nonce"] = signed_order.nonce;
        order_json["feeRateBps"] = signed_order.fee_rate_bps;
        order_json["side"] = signed_order.side;
        order_json["signatureType"] = signed_order.signature_type;
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
            std::string yes_token;
            std::string market_slug;
            uint64_t now_ts = static_cast<uint64_t>(std::time(nullptr));
            uint64_t min_time_left = 2 * 60; // 2 minutes minimum

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

            double best_ask = 0.0;
            bool is_neg_risk = false; // Cache neg_risk during discovery
            for (const auto &[target_ts, expiry_ts] : candidates)
            {
                std::string slug = "btc-updown-15m-" + std::to_string(target_ts);
                uint64_t time_left = expiry_ts - now_ts;

                HttpClient gamma_http;
                gamma_http.set_base_url("https://gamma-api.polymarket.com");
                gamma_http.set_timeout_ms(10000);

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

                            // Check if this market has liquidity
                            auto book_response = http.get("/book?token_id=" + candidate_token);
                            if (book_response.ok())
                            {
                                auto book_json = json::parse(book_response.body);
                                if (book_json.contains("asks") && !book_json["asks"].empty())
                                {
                                    best_ask = std::stod(book_json["asks"][0]["price"].get<std::string>());
                                    if (best_ask > 0.0 && best_ask < 1.0)
                                    {
                                        yes_token = candidate_token;
                                        market_slug = slug;

                                        // Cache neg_risk during discovery (not during order placement)
                                        auto neg_risk_response = http.get("/neg-risk?token_id=" + candidate_token);
                                        if (neg_risk_response.ok())
                                        {
                                            auto neg_risk_json = json::parse(neg_risk_response.body);
                                            is_neg_risk = neg_risk_json.value("neg_risk", false);
                                        }

                                        std::cout << "    Found market with liquidity: " << slug << " (expires in " << time_left / 60 << "min)\n";
                                        std::cout << "    Best ask: " << best_ask << "\n";
                                        std::cout << "    neg_risk: " << (is_neg_risk ? "true" : "false") << "\n";
                                        break;
                                    }
                                }
                            }
                            std::cout << "    Skipping " << slug << " - no liquidity\n";
                        }
                    }
                }
            }

            if (yes_token.empty() || best_ask <= 0.0)
            {
                std::cerr << "    Could not find active BTC 15m market with liquidity\n";
                http_global_cleanup();
                return 1;
            }

            std::cout << "    YES token: " << yes_token.substr(0, 30) << "...\n";

            // Use cached neg_risk value from discovery
            std::string exchange_address = is_neg_risk ? NEG_RISK_CTF_EXCHANGE : CTF_EXCHANGE;
            std::cout << "    Exchange: " << exchange_address << "\n";

            // Calculate shares for $1 order - match TS client's rounding for tick size 0.01
            // Config: price=2, size=2, amount=4
            double order_usd = 1.0;
            double raw_price = std::floor(best_ask * 100) / 100;  // roundDown to 2 decimals
            double raw_maker = std::floor(order_usd * 100) / 100; // roundDown to 2 decimals
            double raw_taker = raw_maker / raw_price;

            // TS rounding: roundUp to (amount+4)=8 decimals, then roundDown to amount=4 decimals
            raw_taker = std::ceil(raw_taker * 100000000) / 100000000; // roundUp to 8 decimals
            raw_taker = std::floor(raw_taker * 10000) / 10000;        // roundDown to 4 decimals

            std::cout << "    Placing FAK order: $" << order_usd << " @ " << raw_price << " = " << raw_taker << " shares\n";

            // Create order
            OrderData real_order;
            real_order.maker = funder_address;
            real_order.taker = "0x0000000000000000000000000000000000000000";
            real_order.token_id = yes_token;
            real_order.maker_amount = to_wei(raw_maker, 6);
            real_order.taker_amount = to_wei(raw_taker, 6);
            real_order.side = OrderSide::BUY;
            real_order.fee_rate_bps = "0";
            real_order.nonce = "0";
            real_order.signer = signer.address();
            real_order.expiration = "0";
            // Use POLY_GNOSIS_SAFE (2) when funder != signer (proxy wallet)
            real_order.signature_type = (funder_address != signer.address())
                                            ? SignatureType::POLY_GNOSIS_SAFE
                                            : SignatureType::EOA;

            // Debug: print order data before signing
            std::cout << "    Order data for signing:\n";
            std::cout << "      maker: " << real_order.maker << "\n";
            std::cout << "      signer: " << real_order.signer << "\n";
            std::cout << "      taker: " << real_order.taker << "\n";
            std::cout << "      tokenId: " << real_order.token_id << "\n";
            std::cout << "      makerAmount: " << real_order.maker_amount << "\n";
            std::cout << "      takerAmount: " << real_order.taker_amount << "\n";
            std::cout << "      side: " << static_cast<int>(real_order.side) << "\n";
            std::cout << "      signatureType: " << static_cast<int>(real_order.signature_type) << "\n";
            std::cout << "      exchange: " << exchange_address << "\n";

            auto real_signed = signer.sign_order(real_order, exchange_address);

            // Build POST body - must match TS client's orderToJson format exactly
            // Use ordered_json to preserve field order (API may be sensitive to this)
            nlohmann::ordered_json post_body;
            nlohmann::ordered_json order_obj;
            order_obj["salt"] = std::stoll(real_signed.salt);
            order_obj["maker"] = real_signed.maker;
            order_obj["signer"] = real_signed.signer;
            order_obj["taker"] = real_signed.taker;
            order_obj["tokenId"] = real_signed.token_id;
            order_obj["makerAmount"] = real_signed.maker_amount;
            order_obj["takerAmount"] = real_signed.taker_amount;
            order_obj["side"] = real_signed.side == 0 ? "BUY" : "SELL";
            order_obj["expiration"] = real_signed.expiration;
            order_obj["nonce"] = real_signed.nonce;
            order_obj["feeRateBps"] = real_signed.fee_rate_bps;
            order_obj["signatureType"] = real_signed.signature_type;
            order_obj["signature"] = real_signed.signature;
            post_body["deferExec"] = false;
            post_body["order"] = order_obj;
            post_body["owner"] = creds.api_key;
            post_body["orderType"] = "FAK";

            std::string body_str = post_body.dump();
            std::cout << "    Full order body:\n"
                      << post_body.dump(2) << "\n";

            HttpClient order_http;
            order_http.set_base_url(CLOB_API);
            order_http.set_timeout_ms(15000);

            std::map<std::string, std::string> post_headers;
            post_headers["Content-Type"] = "application/json";

            // Add L2 auth headers if we have credentials
            if (have_creds)
            {
                auto l2 = signer.generate_l2_headers(creds, "POST", "/order", body_str, funder_address);
                post_headers["POLY_ADDRESS"] = l2.poly_address;
                post_headers["POLY_SIGNATURE"] = l2.poly_signature;
                post_headers["POLY_TIMESTAMP"] = l2.poly_timestamp;
                post_headers["POLY_API_KEY"] = l2.poly_api_key;
                post_headers["POLY_PASSPHRASE"] = l2.poly_passphrase;
                std::cout << "    Using L2 auth with address: " << l2.poly_address << "\n";
            }

            auto post_response = order_http.post("/order", body_str, post_headers);

            std::cout << "\n    Order placement response: " << post_response.status_code << "\n";
            std::cout << "    Response: " << post_response.body << "\n";

            if (post_response.ok())
            {
                auto result = json::parse(post_response.body);
                if (result.contains("success") && result["success"].get<bool>())
                {
                    std::cout << "\n    ✅ ORDER PLACED SUCCESSFULLY!\n";
                    std::cout << "    Order ID: " << result["orderID"].get<std::string>() << "\n";
                    if (result.contains("status"))
                        std::cout << "    Status: " << result["status"].get<std::string>() << "\n";
                    if (result.contains("makingAmount"))
                        std::cout << "    Cost: $" << result["makingAmount"].get<std::string>() << "\n";
                    if (result.contains("takingAmount"))
                        std::cout << "    Shares: " << result["takingAmount"].get<std::string>() << "\n";
                }
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
