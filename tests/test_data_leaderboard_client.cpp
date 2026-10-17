#include "../src/clob_client_test_fixture.hpp"
#include "data_leaderboard_fixtures.hpp"
#include "polymarket/data_client.hpp"

#include <string>
#include <vector>

namespace clob_test
{
    namespace
    {
        const std::string user = "0x2222222222222222222222222222222222222222";
        constexpr const char *empty_page =
            R"({"data":[],"pagination":{"has_more":false,"next_cursor":null}})";

        Environment local_environment(const std::string &url)
        {
            auto environment = Environment::production();
            environment.data_url = url;
            return environment;
        }

        bool test_leaderboards()
        {
            LocalServer server;
            DataClient client(local_environment(server.url()));
            server.enqueue(data_fixtures::data_trader_leaderboard_page);
            server.enqueue(empty_page);
            server.enqueue(data_fixtures::data_trader_standing);
            server.enqueue(R"({"data":null})");
            server.enqueue(data_fixtures::data_biggest_winners_page);
            server.enqueue(empty_page);
            DataLeaderboardFilter weekly;
            weekly.category = "SPORTS";
            weekly.window = "week";
            weekly.sort_by = "VOLUME";
            auto traders = client.trader_leaderboard_pager(weekly, 2).collect();
            DataLeaderboardFilter standing_filter;
            standing_filter.window = "all";
            auto standing = client.get_trader_standing(user, standing_filter);
            auto unknown = client.get_trader_standing(user);
            DataLeaderboardFilter monthly;
            monthly.window = "month";
            auto winners = client.biggest_winners_pager(monthly, 3).collect();
            const auto requests = server.requests();
            return check(traders.ok() && traders.value().size() == 2,
                         "trader leaderboard must collect every page") &&
                   check(standing.ok() && standing.value() && standing.value()->volume_rank == 36,
                         "standing must parse") &&
                   check(unknown.ok() && !unknown.value(), "an unknown trader must have none") &&
                   check(winners.ok() && winners.value().size() == 3,
                         "biggest winners must collect every page") &&
                   check(requests.size() == 6 &&
                             requests[0].target == "/v2/leaderboard?category=SPORTS"
                                                   "&time_period=week&sort_by=VOLUME&limit=2" &&
                             requests[1].target.find("&cursor=") != std::string::npos &&
                             requests[2].target ==
                                 "/v2/leaderboard?user=" + user + "&time_period=all" &&
                             requests[3].target == "/v2/leaderboard?user=" + user &&
                             requests[4].target ==
                                 "/v2/biggest-winners?time_period=month&limit=3" &&
                             requests[5].target.starts_with("/v2/biggest-winners?") &&
                             requests[5].target.find("&cursor=") != std::string::npos,
                         "leaderboards must send the official paths and filters");
        }

        bool test_invalid_arguments()
        {
            LocalServer server;
            DataClient client(local_environment(server.url()));
            const auto rejected_page = [](auto pager)
            {
                auto page = pager.next_page();
                return !page.ok() && page.error().code == SdkErrorCode::InvalidArgument;
            };
            DataLeaderboardFilter bad_window;
            bad_window.window = "year";
            DataLeaderboardFilter bad_sort;
            bad_sort.sort_by = "RANK";
            DataLeaderboardFilter sorted;
            sorted.sort_by = "PNL";
            const auto standing_rejected =
                [&](const std::string &who, const DataLeaderboardFilter &filter)
            {
                auto result = client.get_trader_standing(who, filter);
                return !result.ok() && result.error().code == SdkErrorCode::InvalidArgument;
            };
            const bool rejected =
                rejected_page(client.trader_leaderboard_pager(bad_window)) &&
                rejected_page(client.trader_leaderboard_pager(bad_sort)) &&
                rejected_page(client.trader_leaderboard_pager({}, 1001)) &&
                rejected_page(client.biggest_winners_pager(bad_window)) &&
                rejected_page(client.biggest_winners_pager(sorted)) &&
                rejected_page(client.biggest_winners_pager({}, 0)) && standing_rejected("", {}) &&
                standing_rejected(user, bad_window) && standing_rejected(user, sorted);
            return check(rejected,
                         "invalid leaderboard arguments must fail with InvalidArgument") &&
                   check(server.requests().empty(), "invalid arguments must not send requests");
        }
    } // namespace
} // namespace clob_test

int main()
{
    using namespace clob_test;
    const bool passed = test_leaderboards() && test_invalid_arguments();
    if (!passed) return 1;
    std::cout << "test_data_leaderboard_client passed\n";
    return 0;
}
