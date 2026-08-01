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
#include "order_test_live.hpp"
#include "http_client.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
#include <cstdlib>
#include <string>

using json = nlohmann::json;
using namespace polymarket;

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
        auto signed_order = signer.sign_order(order, order_test::NEG_RISK_CTF_EXCHANGE);

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
        auto signed_order_fixed = signer.sign_order_with_salt(fixed_order, order_test::NEG_RISK_CTF_EXCHANGE, fixed_salt);
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
        http.set_base_url(order_test::CLOB_API);
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
            std::cout << "\n[5] Attempting to derive API credentials for signer: " << signer.address() << "\n";
            try
            {
                creds = live_mode
                            ? signer.create_or_derive_api_credentials(http)
                            : signer.derive_api_credentials(http);
                std::cout << "    API credentials derived\n";
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
            // Test fetching open orders (requires L2 auth)
            std::cout << "\n[6] Testing authenticated API call (GET /data/orders)...\n";

            // Generate L2 headers for the actual endpoint we're calling
            auto headers = signer.generate_l2_headers(creds, "GET", "/data/orders", "", funder_address);
            std::cout << "    POLY_ADDRESS: " << headers.poly_address << "\n";

            HttpClient auth_http;
            auth_http.set_base_url(order_test::CLOB_API);
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
            if (!order_test::run_live_order(private_key, funder_address, creds, have_creds, signer))
            {
                http_global_cleanup();
                return 1;
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
