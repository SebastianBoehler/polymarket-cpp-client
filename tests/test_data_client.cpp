#include "../src/clob_client_test_fixture.hpp"
#include "data_fixtures.hpp"
#include "polymarket/data_client.hpp"

#include <string>
#include <vector>

namespace clob_test
{
    namespace
    {
        const std::string user = "0x1111111111111111111111111111111111111111";
        const std::string condition =
            "0xefa17dee3af09f69f9ddf245b969aa4efbe7c71cdf06ee49d694408bc33e2ed2";
        constexpr const char *empty_page =
            R"({"data":[],"pagination":{"has_more":false,"next_cursor":null}})";

        Environment local_environment(const std::string &url)
        {
            auto environment = Environment::production();
            environment.data_url = url;
            return environment;
        }

        std::string upper(std::string text)
        {
            for (std::size_t i = 2; i < text.size(); ++i)
                text[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(text[i])));
            return text;
        }

        bool test_trades_pager()
        {
            LocalServer server;
            DataClient client(local_environment(server.url()));
            server.enqueue(data_fixtures::data_trade_page);
            server.enqueue(empty_page);
            server.enqueue(empty_page);
            DataTradeFilter filter;
            filter.user = user;
            filter.condition_ids = {condition, upper(condition)};
            filter.side = "SELL";
            filter.taker_only = true;
            filter.filter_type = "CASH";
            filter.filter_amount = 10.0;
            filter.start = 1700000000;
            auto pager = client.trades_pager(filter, 50);
            auto trades = pager.collect();
            DataTradeFilter history;
            history.full_history = true;
            (void)client.trades_pager(history).collect();
            const auto requests = server.requests();
            const auto cursor = nlohmann::json::parse(data_fixtures::data_trade_page)
                                    .at("pagination")
                                    .at("next_cursor")
                                    .get<std::string>();
            return check(trades.ok() && trades.value().size() == 3,
                         "trades pager must collect every page") &&
                   check(
                       requests.size() == 3 &&
                           requests[0].target ==
                               "/v2/trades?user=" + user + "&condition_id=" + condition +
                                   "&side=SELL&taker_only=true&filter_type=CASH&filter_amount=10.0"
                                   "&start=1700000000&limit=50",
                       "trades pager must send the official filters once per distinct condition") &&
                   check(requests[1].target.ends_with("&limit=50&cursor=" + cursor),
                         "trades pager must follow the cursor") &&
                   check(requests[2].target == "/v2/trades?start=1&limit=100",
                         "full_history must send start=1") &&
                   check(pager.resume_token().exhausted,
                         "trades pager must end when has_more is false");
        }

        bool test_activity_pager()
        {
            LocalServer server;
            DataClient client(local_environment(server.url()));
            server.enqueue(data_fixtures::data_activity_last_page);
            DataActivityFilter filter;
            filter.event_ids = {5, 5, 7};
            filter.types = {"TRADE", "REDEEM"};
            filter.sort_direction = "ASC";
            auto activity = client.activity_pager(user, filter).collect();
            const auto requests = server.requests();
            return check(activity.ok() && activity.value().size() == 1,
                         "activity pager must parse") &&
                   check(requests.size() == 1 &&
                             requests[0].target ==
                                 "/v2/activity?user=" + user +
                                     "&event_id=5%2C7&type=TRADE%2CREDEEM&sort_direction=ASC"
                                     "&exclude_deposits_withdrawals=false&limit=100",
                         "activity pager must comma-join lists and keep deposits and withdrawals");
        }

        bool test_invalid_arguments()
        {
            LocalServer server;
            DataClient client(local_environment(server.url()));
            std::vector<DataTradeFilter> bad_trades(8);
            bad_trades[0].condition_ids = {condition};
            bad_trades[0].event_ids = {1};
            for (int i = 0; i < 21; ++i)
            {
                const char hex[] = "0123456789abcdef";
                bad_trades[1].condition_ids.push_back(condition.substr(0, 64) + hex[i / 16] +
                                                      hex[i % 16]);
            }
            bad_trades[2].condition_ids = {"0xabc"};
            bad_trades[3].event_ids = {0};
            bad_trades[4].full_history = true;
            bad_trades[4].start = 5;
            bad_trades[5].side = "HOLD";
            bad_trades[6].filter_amount = -1.0;
            bad_trades[7].end = 253402300800;
            bool all_rejected = true;
            for (const auto &filter : bad_trades)
            {
                auto page = client.trades_pager(filter).next_page();
                all_rejected = all_rejected && !page.ok() &&
                               page.error().code == SdkErrorCode::InvalidArgument;
            }
            DataActivityFilter bad_type;
            bad_type.types = {"BAD"};
            auto no_user = client.activity_pager("").next_page();
            auto unknown_type = client.activity_pager(user, bad_type).next_page();
            auto oversized = client.trades_pager({}, 1001).next_page();
            auto zero = client.activity_pager(user, {}, 0).next_page();
            return check(all_rejected, "invalid trade filters must fail with InvalidArgument") &&
                   check(!no_user.ok() && !unknown_type.ok() && !oversized.ok() && !zero.ok() &&
                             oversized.error().code == SdkErrorCode::InvalidArgument,
                         "invalid activity arguments and page sizes must fail with "
                         "InvalidArgument") &&
                   check(server.requests().empty(), "invalid arguments must not send requests");
        }

        bool test_failures_and_retry()
        {
            LocalServer server;
            DataClient client(local_environment(server.url()));
            server.enqueue(R"({"error":"slow down"})", 429, {{"Retry-After", "0"}});
            server.enqueue(empty_page);
            server.enqueue(
                R"({"error":"limit must be between 0 and 1000","code":"invalid_request"})", 400);
            auto retried = client.trades_pager().collect();
            auto failed = client.activity_pager(user).collect();
            return check(retried.ok() && server.requests().size() >= 2,
                         "a rate-limited page must be retried") &&
                   check(!failed.ok() && failed.error().code == SdkErrorCode::ApiResponse &&
                             failed.error().http_status == 400 &&
                             failed.error().endpoint == "/v2/activity",
                         "an API error must surface with its status and endpoint");
        }
    } // namespace
} // namespace clob_test

int main()
{
    using namespace clob_test;
    const bool passed = test_trades_pager() && test_activity_pager() && test_invalid_arguments() &&
                        test_failures_and_retry();
    if (!passed) return 1;
    std::cout << "test_data_client passed\n";
    return 0;
}
