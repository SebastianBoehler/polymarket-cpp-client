#include "../src/clob_client_test_fixture.hpp"
#include "data_portfolio_fixtures.hpp"
#include "polymarket/data_client.hpp"

#include <string>
#include <vector>

namespace clob_test
{
    namespace
    {
        const std::string user = "0x2222222222222222222222222222222222222222";
        const std::string condition =
            "0x276b505d7bced5a20bf3f91d35dd425d90d3881b3daddbc49433b9f045f101e0";
        const std::string structural =
            "0x01aa000000000000000000000000000000000000000000000000000000000b";
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

        bool test_positions_pager()
        {
            LocalServer server;
            DataClient client(local_environment(server.url()));
            server.enqueue(data_fixtures::data_position_page);
            server.enqueue(empty_page);
            server.enqueue(empty_page);
            DataPositionFilter filter;
            filter.user = user;
            filter.condition_ids = {upper(condition), condition, upper(structural)};
            filter.status = "OPEN";
            filter.title = "Esports & more";
            filter.filter_type = "TOKENS";
            filter.filter_amount = 1.0;
            filter.include_archived = false;
            filter.sort_by = "TOTAL_PNL";
            filter.sort_direction = "DESC";
            filter.end = 1800000000;
            auto pager = client.positions_pager(filter, 2);
            auto positions = pager.collect();
            DataPositionFilter market;
            market.condition_ids = {condition};
            market.title = "   ";
            (void)client.positions_pager(market).collect();
            const auto requests = server.requests();
            return check(positions.ok() && positions.value().size() == 3,
                         "positions pager must collect every page") &&
                   check(requests.size() == 3 &&
                             requests[0].target ==
                                 "/v2/positions?user=" + user + "&condition_id=" + condition +
                                     "%2C" + structural + "00" +
                                     "&status=OPEN&title=Esports%20%26%20more&filter_type=TOKENS"
                                     "&filter_amount=1.0&include_archived=false&sort_by=TOTAL_PNL"
                                     "&sort_direction=DESC&end=1800000000&limit=2",
                         "positions pager must send lowercased, padded and distinct market IDs") &&
                   check(requests[1].target.find("&cursor=") != std::string::npos,
                         "positions pager must follow the cursor") &&
                   check(requests[2].target ==
                             "/v2/positions?condition_id=" + condition + "&limit=100",
                         "a market listing must not need a user or send a blank title");
        }

        bool test_portfolio_reads()
        {
            LocalServer server;
            DataClient client(local_environment(server.url()));
            server.enqueue(data_fixtures::data_portfolio_value);
            server.enqueue(data_fixtures::data_user_stats);
            server.enqueue(R"({"data":null})");
            server.enqueue(data_fixtures::data_pnl_series);
            server.enqueue(data_fixtures::data_user_volume);
            const std::string zip("PK\x03\x04\x00\x00\xff\x01", 8);
            server.enqueue(zip, 200, {{"Content-Type", "application/zip"}});
            auto value = client.get_portfolio_value(user, {condition});
            auto stats = client.get_user_stats(user);
            auto none = client.get_user_stats(user);
            auto pnl = client.get_user_pnl(user, "1d", "1h");
            DataTimeRange range;
            range.full_history = true;
            auto volume = client.get_user_volume(user, range);
            auto snapshot = client.download_accounting_snapshot(user);
            const auto requests = server.requests();
            const std::vector<std::string> expected = {
                "/v2/value?user=" + user + "&condition_id=" + condition,
                "/v2/user-stats?user=" + user,
                "/v2/user-stats?user=" + user,
                "/v2/user-pnl?user=" + user + "&interval=1d&fidelity=1h",
                "/v2/user-volume?user=" + user + "&start=1",
                "/v1/accounting/snapshot?user=" + user,
            };
            bool targets = requests.size() == expected.size();
            for (std::size_t i = 0; targets && i < expected.size(); ++i)
                targets = requests[i].target == expected[i];
            return check(value.ok() && value.value().value > 45000.0, "value must parse") &&
                   check(stats.ok() && stats.value() && stats.value()->views == 4356,
                         "stats must parse") &&
                   check(none.ok() && !none.value(), "null stats must read as no stats") &&
                   check(pnl.ok() && pnl.value().points.size() == 2, "PnL must parse") &&
                   check(volume.ok() && volume.value().trade_count == 6359523,
                         "volume must parse") &&
                   check(snapshot.ok() && snapshot.value() == zip,
                         "snapshot must keep every byte") &&
                   check(targets, "portfolio reads must send the official paths and filters");
        }

        bool test_invalid_arguments()
        {
            LocalServer server;
            DataClient client(local_environment(server.url()));
            std::vector<DataPositionFilter> bad(10);
            bad[0].condition_ids = {condition, "0x" + condition.substr(4)};
            bad[1].event_ids = {1};
            bad[1].condition_ids = {condition};
            bad[2].user = user;
            bad[2].condition_ids = {"0x03" + condition.substr(4, 60)};
            bad[3].condition_ids = {condition};
            bad[3].status = "MERGEABLE";
            bad[4].user = user;
            bad[4].status = "CLOSED";
            bad[4].include_archived = true;
            bad[5].user = user;
            bad[5].title = std::string(201, 'a');
            bad[6].user = user;
            bad[6].sort_by = "SIZE";
            bad[7].user = user;
            bad[7].start = 0;
            bad[8].condition_ids = {condition};
            bad[8].event_ids = {3};
            bad[9] = {};
            bool all_rejected = true;
            for (const auto &filter : bad)
            {
                auto page = client.positions_pager(filter).next_page();
                all_rejected = all_rejected && !page.ok() &&
                               page.error().code == SdkErrorCode::InvalidArgument;
            }
            DataPositionFilter long_title;
            long_title.user = user;
            for (int i = 0; i < 200; ++i)
                long_title.title += "\xc3\xa9";
            DataTimeRange mixed;
            mixed.full_history = true;
            mixed.end = 5;
            const auto rejected = [](const auto &result)
            { return !result.ok() && result.error().code == SdkErrorCode::InvalidArgument; };
            const bool reads_rejected = rejected(client.get_portfolio_value("")) &&
                                        rejected(client.get_portfolio_value(user, {"0x12"})) &&
                                        rejected(client.get_user_stats("")) &&
                                        rejected(client.get_user_pnl(user, "2d")) &&
                                        rejected(client.get_user_pnl(user, "", "5m")) &&
                                        rejected(client.get_user_volume(user, mixed)) &&
                                        rejected(client.download_accounting_snapshot(""));
            const bool no_requests = server.requests().empty();
            server.enqueue(empty_page);
            auto accented = client.positions_pager(long_title).next_page();
            return check(all_rejected, "invalid position filters must fail with InvalidArgument") &&
                   check(reads_rejected,
                         "invalid portfolio reads must fail with InvalidArgument") &&
                   check(no_requests, "invalid arguments must not send requests") &&
                   check(accented.ok(), "a title limit must count characters, not bytes");
        }

        bool test_failures()
        {
            LocalServer server;
            DataClient client(local_environment(server.url()));
            server.enqueue(R"({"error":"user is invalid"})", 400);
            server.enqueue(R"({"data":{"volume":1}})");
            auto failed = client.download_accounting_snapshot(user);
            auto malformed = client.get_user_volume(user);
            return check(!failed.ok() && failed.error().code == SdkErrorCode::ApiResponse &&
                             failed.error().http_status == 400 &&
                             failed.error().endpoint == "/v1/accounting/snapshot",
                         "a failed download must surface its status and endpoint") &&
                   check(!malformed.ok() && malformed.error().code == SdkErrorCode::Parse,
                         "a malformed read must fail with Parse");
        }
    } // namespace
} // namespace clob_test

int main()
{
    using namespace clob_test;
    const bool passed = test_positions_pager() && test_portfolio_reads() &&
                        test_invalid_arguments() && test_failures();
    if (!passed) return 1;
    std::cout << "test_data_portfolio_client passed\n";
    return 0;
}
