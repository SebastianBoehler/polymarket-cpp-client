#include "../src/clob_client_test_fixture.hpp"

#include <stdexcept>
#include <string>
#include <vector>

namespace clob_test
{
    namespace
    {
        std::string market_page(const std::string &condition_id, const std::string &next_cursor)
        {
            return R"({"next_cursor":")" + next_cursor + R"(","data":[{"condition_id":")" +
                   condition_id +
                   R"(","active":true,"closed":false,"tokens":[{"token_id":"yes","outcome":"Yes"},{"token_id":"no","outcome":"No"}]}]})";
        }

        Environment local_environment(const std::string &url)
        {
            auto environment = Environment::production();
            environment.clob_url = url;
            environment.data_url = url;
            return environment;
        }

        bool test_markets_pager_follows_cursor()
        {
            LocalServer server;
            ClobClient client(server.url(), 137);
            server.enqueue(market_page("first", "N+/="));
            server.enqueue(market_page("second", "LTE="));
            auto pager = client.markets_pager();
            std::vector<std::string> ids;
            for (const auto &market : pager.items())
                ids.push_back(market.condition_id);
            const auto requests = server.requests();
            return check(ids == std::vector<std::string>({"first", "second"}),
                         "markets pager must cover both pages") &&
                   check(requests.size() == 2 &&
                             requests[0].target == "/markets?next_cursor=MA%3D%3D" &&
                             requests[1].target == "/markets?next_cursor=N%2B%2F%3D",
                         "markets pager must encode each cursor") &&
                   check(pager.done() && !pager.error(), "markets pager must end cleanly");
        }

        bool test_market_pager_variants_and_resume()
        {
            LocalServer server;
            ClobClient client(server.url(), 137);
            server.enqueue(market_page("a", "LTE="));
            server.enqueue(market_page("b", "LTE="));
            server.enqueue(market_page("c", "LTE="));
            server.enqueue(market_page("d", "LTE="));
            (void)client.sampling_markets_pager().collect();
            (void)client.simplified_markets_pager().collect();
            (void)client.sampling_simplified_markets_pager().collect();
            auto resumed = client.markets_pager("saved");
            const auto markets = resumed.collect();
            const auto requests = server.requests();
            return check(requests.size() == 4 &&
                             requests[0].target == "/sampling-markets?next_cursor=MA%3D%3D" &&
                             requests[1].target == "/simplified-markets?next_cursor=MA%3D%3D" &&
                             requests[2].target ==
                                 "/sampling-simplified-markets?next_cursor=MA%3D%3D",
                         "market pager variants must use their endpoints") &&
                   check(requests[3].target == "/markets?next_cursor=saved",
                         "markets pager must start at start_cursor") &&
                   check(markets.ok() && markets.value().size() == 1,
                         "resumed markets pager must load");
        }

        bool test_markets_pager_reports_http_failure()
        {
            LocalServer server;
            ClobClient client(server.url(), 137);
            server.enqueue(market_page("kept", "N+/="));
            server.enqueue(R"({"error":"boom"})", 500);
            auto pager = client.markets_pager();
            std::vector<std::string> ids;
            for (const auto &market : pager.items())
                ids.push_back(market.condition_id);
            return check(ids == std::vector<std::string>({"kept"}),
                         "items before a failure must be yielded") &&
                   check(pager.error() && pager.error()->http_status == 500 &&
                             pager.error()->endpoint == "/markets",
                         "markets pager must expose the HTTP failure");
        }

        bool test_open_orders_pager_signs_and_resumes()
        {
            LocalServer server;
            auto client = authenticated_client(server.url());
            server.enqueue(R"({"next_cursor":"LTE=","data":[]})");
            const std::string market = "m+/=";
            auto pager = client.open_orders_pager(market, "C2");
            const auto orders = pager.collect();
            const auto requests = server.requests();
            return check(orders.ok() && orders.value().empty(),
                         "open-orders pager must load an empty page") &&
                   check(requests.size() == 1 &&
                             requests[0].target == "/data/orders?market=m%2B%2F%3D&next_cursor=C2",
                         "open-orders pager must keep the market filter and start cursor") &&
                   check(requests[0].headers.at("poly_signature") ==
                             expected_signature(requests[0], "/data/orders"),
                         "open-orders pager must sign the endpoint path");
        }

        bool test_authenticated_pagers_require_credentials()
        {
            LocalServer server;
            ClobClient client(server.url(), 137);
            auto orders = client.open_orders_pager().next_page();
            auto trades = client.trades_pager().collect();
            bool legacy_threw = false;
            try
            {
                (void)client.get_trades();
            }
            catch (const std::runtime_error &)
            {
                legacy_threw = true;
            }
            return check(!orders.ok() && orders.error().code == SdkErrorCode::Auth,
                         "open-orders pager must fail with Auth") &&
                   check(!trades.ok() && trades.error().code == SdkErrorCode::Auth,
                         "trades pager must fail with Auth") &&
                   check(legacy_threw, "legacy get_trades must still throw when unauthenticated") &&
                   check(server.requests().empty(),
                         "unauthenticated pagers must not send requests");
        }

        bool test_trades_pager_follows_cursor()
        {
            LocalServer server;
            auto client = authenticated_client(server.url());
            server.enqueue(R"({"next_cursor":"T2","data":[]})");
            server.enqueue(R"({"next_cursor":"LTE=","data":[]})");
            auto pager = client.trades_pager("T1");
            const auto trades = pager.collect();
            const auto requests = server.requests();
            return check(trades.ok(), "trades pager must succeed") &&
                   check(requests.size() == 2 &&
                             requests[0].target == "/data/trades?next_cursor=T1" &&
                             requests[1].target == "/data/trades?next_cursor=T2",
                         "trades pager must follow cursors from start_cursor") &&
                   check(pager.resume_token().cursor == "LTE=",
                         "exhausted trades pager must hold the end cursor");
        }

        bool test_positions_pager_offsets_and_address()
        {
            LocalServer server;
            ClobClient client(local_environment(server.url()));
            auto missing = client.positions_pager().next_page();
            const bool missing_rejected = !missing.ok() &&
                                          missing.error().code == SdkErrorCode::InvalidArgument &&
                                          server.requests().empty();

            server.enqueue(R"([])");
            server.enqueue(R"([])");
            server.enqueue(R"([])");
            const std::string user = "0x1111111111111111111111111111111111111111";
            auto all = client.positions_pager(user, 500).collect();
            (void)client.redeemable_positions_pager(user).collect();
            (void)client.mergeable_positions_pager(user).collect();
            const auto requests = server.requests();
            return check(missing_rejected,
                         "positions pager must reject a missing address before any request") &&
                   check(all.ok() && all.value().empty(),
                         "positions pager must load an empty page") &&
                   check(requests.size() == 3 && requests[0].target == "/positions?user=" + user +
                                                                           "&limit=500&offset=500",
                         "positions pager must start at start_offset") &&
                   check(requests[1].target == "/positions?user=" + user +
                                                   "&redeemable=true&limit=500&offset=0" &&
                             requests[2].target ==
                                 "/positions?user=" + user + "&mergeable=true&limit=500&offset=0",
                         "position pager variants must keep their filters");
        }
    } // namespace
} // namespace clob_test

int main()
{
    using namespace clob_test;
    const bool passed =
        test_markets_pager_follows_cursor() && test_market_pager_variants_and_resume() &&
        test_markets_pager_reports_http_failure() && test_open_orders_pager_signs_and_resumes() &&
        test_authenticated_pagers_require_credentials() && test_trades_pager_follows_cursor() &&
        test_positions_pager_offsets_and_address();
    if (!passed) return 1;
    std::cout << "test_clob_pagers passed\n";
    return 0;
}
