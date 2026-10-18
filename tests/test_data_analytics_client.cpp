#include "../src/clob_client_test_fixture.hpp"
#include "data_analytics_fixtures.hpp"
#include "polymarket/data_client.hpp"

#include <chrono>
#include <string>
#include <vector>

namespace clob_test
{
    namespace
    {
        const std::string condition =
            "0x276b505d7bced5a20bf3f91d35dd425d90d3881b3daddbc49433b9f045f101e0";
        const std::string other =
            "0x60613b262912ce2e3138ec5610a31a2f932b6300843c094053cb33aaff9f2238";
        const std::string asset =
            "84586182553654676324128999229937144421794625234046892536021543616252589144969";
        constexpr const char *empty_page =
            R"({"data":[],"pagination":{"has_more":false,"next_cursor":null}})";

        Environment local_environment(const std::string &url)
        {
            auto environment = Environment::production();
            environment.data_url = url;
            return environment;
        }

        std::int64_t now()
        {
            return std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                .count();
        }

        bool test_pagers()
        {
            LocalServer server;
            DataClient client(local_environment(server.url()));
            server.enqueue(data_fixtures::data_holders_page);
            server.enqueue(empty_page);
            server.enqueue(data_fixtures::data_price_history_page);
            server.enqueue(empty_page);
            server.enqueue(empty_page);
            DataHolderFilter pnl;
            pnl.include_pnl = true;
            pnl.min_balance = 5.0;
            auto holders = client.holders_pager({condition}, pnl, 2).collect();
            DataPriceHistoryFilter window;
            window.start = 1792197000;
            window.end = 1792197000 + 15 * 86400;
            window.bucket_seconds = 60;
            auto prices = client.price_history_pager(asset, window, 3).collect();
            DataPriceHistoryFilter at;
            at.as_of = 1792262000;
            (void)client.price_history_pager(asset, at).collect();
            const auto requests = server.requests();
            return check(holders.ok() && holders.value().size() == 2 && prices.ok() &&
                             prices.value().size() == 3,
                         "analytics pagers must collect every page") &&
                   check(requests.size() == 5 &&
                             requests[0].target == "/v2/holders?condition_id=" + condition +
                                                       "&min_balance=5.0&include_pnl=true&limit=2",
                         "holders pager must send the official filters") &&
                   check(requests[1].target.starts_with("/v2/holders?") &&
                             requests[1].target.find("&cursor=") != std::string::npos,
                         "holders pager must follow the cursor") &&
                   check(requests[2].target == "/v2/prices-history?token_id=" + asset +
                                                   "&start=1792197000&end=1793493000"
                                                   "&bucket_seconds=60&limit=3",
                         "price history must send a window") &&
                   check(requests[4].target == "/v2/prices-history?token_id=" + asset +
                                                   "&as_of=1792262000&limit=10000",
                         "price history must send as_of with the default page size");
        }

        bool test_reads()
        {
            LocalServer server;
            DataClient client(local_environment(server.url()));
            server.enqueue(data_fixtures::data_open_interests);
            server.enqueue(R"({"data":[]})");
            server.enqueue(data_fixtures::data_live_volume);
            server.enqueue(data_fixtures::data_resolutions);
            server.enqueue(R"({"data":[]})");
            auto interests = client.get_open_interests({condition, other});
            auto global = client.get_open_interests();
            auto volume = client.get_event_live_volume({1202539, 1202539});
            DataResolutionFilter by_condition;
            by_condition.condition_ids = {condition, other};
            auto resolutions = client.get_resolutions(by_condition);
            DataResolutionFilter by_question;
            by_question.question_id = "0x24";
            auto missing = client.get_resolutions(by_question);
            const auto requests = server.requests();
            const std::vector<std::string> expected = {
                "/v2/oi?condition_id=" + condition + "%2C" + other,
                "/v2/oi",
                "/v2/live-volume?event_id=1202539",
                "/v2/resolutions?condition_id=" + condition + "%2C" + other,
                "/v2/resolutions?question_id=0x24",
            };
            bool targets = requests.size() == expected.size();
            for (std::size_t i = 0; targets && i < expected.size(); ++i)
                targets = requests[i].target == expected[i];
            return check(interests.ok() && interests.value().size() == 3, "interests must parse") &&
                   check(global.ok() && global.value().empty(), "an empty list must parse") &&
                   check(volume.ok() && volume.value().markets.size() == 3,
                         "live volume must parse") &&
                   check(resolutions.ok() && resolutions.value().size() == 2,
                         "resolutions must parse") &&
                   check(missing.ok() && missing.value().empty(), "unknown rows must be omitted") &&
                   check(targets, "analytics reads must send the official paths and filters");
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
            const auto rejected = [](const auto &result)
            { return !result.ok() && result.error().code == SdkErrorCode::InvalidArgument; };
            DataHolderFilter pnl;
            pnl.include_pnl = true;
            DataHolderFilter negative;
            negative.min_balance = -1.0;
            const bool holders_rejected =
                rejected_page(client.holders_pager({})) &&
                rejected_page(client.holders_pager({condition, other}, pnl)) &&
                rejected_page(client.holders_pager({condition}, pnl, 101)) &&
                rejected_page(client.holders_pager({condition}, negative)) &&
                rejected_page(client.holders_pager({condition}, {}, 1001));

            std::vector<DataPriceHistoryFilter> bad(9);
            bad[0] = {};
            bad[1].interval = "1d";
            bad[1].start = 5;
            bad[2].interval = "2d";
            bad[3].interval = "1d";
            bad[3].end = 10;
            bad[4].start = 100;
            bad[4].end = 100 + 15 * 86400 + 1;
            bad[5].start = 100;
            bad[5].end = 99;
            bad[6].start = now() - 16 * 86400;
            bad[7].interval = "1w";
            bad[7].bucket_seconds = 299;
            bad[8].as_of = 5;
            bad[8].bucket_seconds = 600;
            bool prices_rejected = rejected_page(client.price_history_pager("", {}));
            for (const auto &filter : bad)
                prices_rejected =
                    prices_rejected && rejected_page(client.price_history_pager(asset, filter));
            DataPriceHistoryFilter as_of;
            as_of.as_of = 5;
            prices_rejected = prices_rejected &&
                              rejected_page(client.price_history_pager(asset, as_of, 100)) &&
                              rejected_page(client.price_history_pager(asset, as_of, 10001));

            DataResolutionFilter two;
            two.question_id = "0x1";
            two.event_ids = {1};
            DataResolutionFilter many;
            for (std::int64_t id = 1; id <= 21; ++id)
                many.event_ids.push_back(id);
            DataResolutionFilter combo;
            combo.condition_ids = {"0x03" + condition.substr(4, 60)};
            const bool reads_rejected =
                rejected(client.get_open_interests({"0x1"})) &&
                rejected(client.get_event_live_volume({})) &&
                rejected(client.get_event_live_volume({0})) &&
                rejected(client.get_resolutions({})) && rejected(client.get_resolutions(two)) &&
                rejected(client.get_resolutions(many)) && rejected(client.get_resolutions(combo));
            return check(holders_rejected, "invalid holder arguments must fail") &&
                   check(prices_rejected, "invalid price history filters must fail") &&
                   check(reads_rejected, "invalid analytics reads must fail") &&
                   check(server.requests().empty(), "invalid arguments must not send requests");
        }
    } // namespace
} // namespace clob_test

int main()
{
    using namespace clob_test;
    const bool passed = test_pagers() && test_reads() && test_invalid_arguments();
    if (!passed) return 1;
    std::cout << "test_data_analytics_client passed\n";
    return 0;
}
