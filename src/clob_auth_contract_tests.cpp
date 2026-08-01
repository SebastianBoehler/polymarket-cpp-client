#include "clob_client_test_fixture.hpp"

namespace clob_test
{
    bool test_signer_only_l1_bootstrap_installs_credentials()
    {
        constexpr const char *private_key =
            "0x0000000000000000000000000000000000000000000000000000000000000001";
        constexpr const char *signer = "0x7E5F4552091A69125d5DfCb7b8C2659029395Bdf";
        constexpr const char *funder = "0x1111111111111111111111111111111111111111";
        LocalServer server;
        bool rejected_before_credentials = false;
        bool ready_before_credentials = true;
        {
            ClobClient signer_only(server.url(), 137, private_key,
                                   SignatureType::POLY_PROXY, funder);
            ready_before_credentials = signer_only.is_authenticated();
            try
            {
                (void)signer_only.get_api_keys();
            }
            catch (const std::runtime_error &)
            {
                rejected_before_credentials = true;
            }
        }
        if (!check(!ready_before_credentials && rejected_before_credentials &&
                       server.requests().empty(),
                   "signer-only client must report unready and reject L2 before credentials"))
            return false;

        HttpClientOptions options;
        options.timeout_ms = 1000;
        ClobClient client(server.url(), 137, private_key, SignatureType::POLY_PROXY,
                          funder, options);
        const bool configured_ready_before_credentials = client.is_authenticated();
        server.enqueue(R"({"apiKey":"derived-key","secret":"c2VjcmV0","passphrase":"derived-pass"})");
        const auto credentials = client.derive_api_key();
        server.enqueue(R"({"apiKeys":["derived-key"]})");
        const auto keys = client.get_api_keys();
        const auto requests = server.requests();

        return check(!configured_ready_before_credentials && client.is_authenticated(),
                     "L2 readiness must become true only after credential installation") &&
               check(credentials.api_key == "derived-key" &&
                         keys == std::vector<std::string>{"derived-key"},
                     "successful L1 derivation must install credentials for L2") &&
               check(client.get_address() == signer && client.get_funder_address() == funder,
                     "proxy signer and funder roles must remain distinct") &&
               check(requests.size() == 2 && requests[0].target == "/auth/derive-api-key" &&
                         requests[1].target == "/auth/api-keys",
                     "L1 bootstrap request sequence mismatch") &&
               check(requests[0].headers.at("poly_address") == signer,
                     "L1 bootstrap must emit signer address") &&
               check(requests[1].headers.at("poly_api_key") == "derived-key" &&
                         requests[1].headers.at("poly_passphrase") == "derived-pass",
                     "L2 request must use installed credentials") &&
               check(requests[1].headers.at("poly_signature") ==
                         expected_signature(requests[1], "/auth/api-keys"),
                     "installed credentials must generate a valid L2 signature");
    }

    bool test_clob_constructors_reject_unsupported_chains()
    {
        constexpr const char *private_key =
            "0x0000000000000000000000000000000000000000000000000000000000000001";
        constexpr const char *funder = "0x1111111111111111111111111111111111111111";
        LocalServer server;
        HttpClientOptions options;
        ApiCredentials credentials{"key", "c2VjcmV0", "pass"};
        const auto rejects = [](auto construct)
        {
            try
            {
                construct();
            }
            catch (const std::invalid_argument &)
            {
                return true;
            }
            catch (...)
            {
            }
            return false;
        };

        const bool all_rejected =
            rejects([&] { ClobClient client(server.url(), 1); }) &&
            rejects([&] { ClobClient client(server.url(), 1, options); }) &&
            rejects([&] { ClobClient client(server.url(), 1, private_key); }) &&
            rejects([&] { ClobClient client(server.url(), 1, private_key,
                                            SignatureType::POLY_PROXY, funder, options); }) &&
            rejects([&] { ClobClient client(server.url(), 1, private_key, credentials); }) &&
            rejects([&] { ClobClient client(server.url(), 1, private_key, credentials,
                                            SignatureType::POLY_PROXY, funder, options); });
        ClobClient polygon(server.url(), 137);
        ClobClient amoy(server.url(), 80002);
        return check(all_rejected, "every CLOB constructor must reject unsupported chains") &&
               check(polygon.get_address().empty() && amoy.get_address().empty(),
                     "Polygon mainnet and Amoy chains must remain supported");
    }

    bool test_clob_constructors_require_non_eoa_funder()
    {
        constexpr const char *key =
            "0x0000000000000000000000000000000000000000000000000000000000000001";
        LocalServer server;
        ApiCredentials credentials{"key", "c2VjcmV0", "pass"};
        const auto rejects = [&](SignatureType type, bool with_credentials)
        {
            try
            {
                if (with_credentials)
                    ClobClient client(server.url(), 137, key, credentials, type);
                else
                    ClobClient client(server.url(), 137, key, type);
            }
            catch (const std::invalid_argument &)
            {
                return true;
            }
            return false;
        };
        return check(rejects(SignatureType::POLY_PROXY, false) &&
                         rejects(SignatureType::POLY_GNOSIS_SAFE, true) &&
                         rejects(SignatureType::POLY_1271, false),
                     "non-EOA clients must require a funder/deposit wallet");
    }

    bool test_clob_warm_connection_uses_time_only()
    {
        LocalServer server;
        ClobClient client(server.url(), 137);
        server.enqueue("1722510000");
        const bool warmed = client.warm_connection();
        server.enqueue("123x");
        const bool trailing = client.warm_connection();
        server.enqueue("-1");
        const bool negative = client.warm_connection();
        server.enqueue("18446744073709551616");
        const bool overflowing = client.warm_connection();
        const auto requests = server.requests();
        return check(warmed, "valid server time must warm the CLOB connection") &&
               check(!trailing && !negative && !overflowing,
                     "server time must be a complete unsigned timestamp") &&
               check(requests.size() == 4 && requests[0].target == "/time",
                     "CLOB warm-up must issue only cheap time requests");
    }

    bool test_order_result_schema_failures()
    {
        LocalServer server;
        auto client = authenticated_client(server.url());
        const auto is_parse_failure = [](const auto &result)
        {
            return !result && result.error().code == SdkErrorCode::Parse;
        };

        server.enqueue("[]");
        const auto order_top_level = client.get_order_result("top-level");
        server.enqueue(R"({"id":7})");
        const auto order_field_type = client.get_order_result("field-type");
        server.enqueue("[]");
        const auto orders_top_level = client.get_open_orders_result();
        server.enqueue(R"({"next_cursor":"LTE=","data":[42]})");
        const auto orders_item = client.get_open_orders_result();
        server.enqueue(R"({"next_cursor":"LTE=","data":[{"id":7}]})");
        const auto orders_field_type = client.get_open_orders_result();
        server.enqueue(R"({"id":"order-1","status":"LIVE","owner":"owner-1","maker_address":"0xmaker","market":"mkt","asset_id":"asset","side":"BUY","original_size":"1","size_matched":"0.5","price":"0.5","associate_trades":["trade-1"],"outcome":"Yes","created_at":1,"expiration":"0","order_type":"GTC"})");
        const auto complete_order = client.get_order_result("order-1");
        server.enqueue(R"({"id":"order-2","status":"LIVE","owner":"owner-2","maker_address":"0xmaker","market":"mkt","asset_id":"asset","side":"BUY","original_size":"1","size_matched":"0","price":"0.5","associate_trades":null,"outcome":"No","created_at":1,"expiration":"0","order_type":"GTC"})");
        const auto null_trades = client.get_order_result("order-2");
        server.enqueue(R"({"id":"order-3","status":"LIVE","owner":"owner-3","maker_address":"0xmaker","market":"mkt","asset_id":"asset","side":"BUY","original_size":"1","size_matched":"0","price":"0.5","outcome":"No","created_at":1,"expiration":"0","order_type":"GTC"})");
        const auto missing_trades = client.get_order_result("order-3");
        server.enqueue(R"({"id":"order-4","status":"LIVE","maker_address":"0xmaker","market":"mkt","asset_id":"asset","side":"BUY","original_size":"1","size_matched":"0","price":"0.5","associate_trades":[],"outcome":"No","created_at":1,"expiration":"0","order_type":"GTC"})");
        const auto missing_owner = client.get_order_result("order-4");

        server.enqueue("[]");
        const auto legacy_order = client.get_order("legacy");
        server.enqueue("[]");
        const auto legacy_orders = client.get_open_orders();

        return check(is_parse_failure(order_top_level),
                     "single-order Result must reject a wrong top-level schema") &&
               check(is_parse_failure(order_field_type),
                     "single-order Result must reject wrong field types") &&
               check(is_parse_failure(orders_top_level),
                     "open-orders Result must reject a wrong top-level schema") &&
               check(is_parse_failure(orders_item),
                     "open-orders Result must reject non-object items") &&
               check(is_parse_failure(orders_field_type),
                     "open-orders Result must reject wrong field types") &&
               check(complete_order && complete_order.value() &&
                         complete_order.value()->owner == "owner-1" &&
                         complete_order.value()->maker_address == "0xmaker" &&
                         complete_order.value()->associate_trades ==
                             std::vector<std::string>{"trade-1"} &&
                         complete_order.value()->outcome == "Yes",
                     "open order must retain identity, trades, and outcome") &&
               check(null_trades && null_trades.value() && missing_trades &&
                         missing_trades.value() &&
                         null_trades.value()->associate_trades.empty() &&
                         missing_trades.value()->associate_trades.empty(),
                     "missing or null associated trades must default to empty") &&
               check(is_parse_failure(missing_owner),
                     "open order must require its owner") &&
               check(!legacy_order && legacy_orders.empty(),
                     "legacy wrappers must continue collapsing parse failures");
    }

    bool test_conditional_balance_allowance_token()
    {
        LocalServer server;
        auto client = authenticated_client(server.url());
        server.enqueue(R"({"balance":"10","allowances":{"0x1111111111111111111111111111111111111111":"20"}})");
        const auto balance = client.get_balance_allowance("CONDITIONAL", "tok+/=");
        server.enqueue(R"({"balance":"10","allowance":"20"})");
        const auto obsolete_singular = client.get_balance_allowance("CONDITIONAL", "tok+/=");
        server.enqueue("OK");
        const bool updated = client.update_balance_allowance("CONDITIONAL", "tok+/=");
        const auto requests = server.requests();
        return check(balance && balance->balance == "10" && balance->allowances.at(
                         "0x1111111111111111111111111111111111111111") == "20" &&
                         !obsolete_singular && updated,
                     "conditional balance methods parse responses") &&
               check(requests.size() == 3, "conditional balance methods issue requests") &&
               check(requests[0].target ==
                         "/balance-allowance?asset_type=CONDITIONAL&signature_type=0&token_id=tok%2B%2F%3D",
                     "conditional balance query includes encoded token") &&
               check(requests[2].target ==
                         "/balance-allowance/update?asset_type=CONDITIONAL&signature_type=0&token_id=tok%2B%2F%3D",
                     "conditional allowance update includes encoded token") &&
               check(requests[0].headers.at("poly_signature") ==
                         expected_signature(requests[0], "/balance-allowance"),
                     "balance query signs the bare endpoint");
    }
}
