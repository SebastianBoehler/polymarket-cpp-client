#include "clob_client_test_fixture.hpp"

#include <string>
#include <vector>

namespace clob_test
{
    bool test_rewards_contracts()
    {
        LocalServer server;
        ClobClient public_client(server.url(), 137);
        server.enqueue(R"({"next_cursor":"N+/=","data":[{"condition_id":"c1","rewards_config":[{"id":7,"asset_address":"0xasset1","start_date":"2026-07-31","end_date":"2500-12-31","rate_per_day":2,"total_rewards":0}],"rewards_max_spread":5.5,"rewards_min_size":20}]})");
        server.enqueue(R"({"next_cursor":"LTE=","data":[{"condition_id":"c2","rewards_config":[{"asset_address":"0xasset2","start_date":"2026-08-01","end_date":"2500-12-31","rate_per_day":"3.5","total_rewards":"10"}],"rewards_max_spread":"4.5","rewards_min_size":"15"}]})");
        const auto current = public_client.get_rewards_markets_current();

        server.enqueue(R"({"next_cursor":"LTE=","data":[{"condition_id":"cond","question":"Question","market_slug":"market","event_slug":"event","image":"image-url","rewards_max_spread":3.5,"rewards_min_size":10,"market_competitiveness":4.25,"tokens":[{"token_id":"1","outcome":"Yes","price":0.6},{"token_id":"2","outcome":"No","price":"0.4"}],"rewards_config":[{"id":"11","asset_address":"0xasset3","start_date":"2026-07-31","end_date":"2500-12-31","rate_per_day":2,"total_rewards":0,"total_days":173279}]}]})");
        const auto raw = public_client.get_rewards_markets("cond");

        auto secure_client = authenticated_client(server.url());
        server.enqueue(R"({"next_cursor":"N+/=","data":[{"date":"2024-01-01","condition_id":"c1","asset_address":"0xasset1","maker_address":"0xmaker","earnings":1.25,"asset_rate":2}]})", 200, 1100);
        server.enqueue(R"({"next_cursor":"LTE=","data":[{"date":"2024-01-01","condition_id":"c2","asset_address":"0xasset2","maker_address":"0xmaker","earnings":"2.5","asset_rate":"3.5"}]})");
        const auto earnings = secure_client.get_earnings_for_user_for_day_all("2024+/=");
        server.enqueue(R"([{"date":"2024-01-01","asset_address":"0xasset3","maker_address":"0xmaker","earnings":3.75,"asset_rate":4}])");
        const auto totals = secure_client.get_total_earnings_for_user_for_day_all("2024+/=");
        const auto requests = server.requests();

        return check(requests.size() == 6, "rewards methods did not follow documented pages") &&
               check(requests[0].target == "/rewards/markets/current?next_cursor=MA%3D%3D" &&
                         requests[1].target == "/rewards/markets/current?next_cursor=N%2B%2F%3D",
                     "current rewards pagination mismatch") &&
               check(requests[2].target == "/rewards/markets/cond?next_cursor=MA%3D%3D",
                     "raw rewards route mismatch") &&
               check(requests[3].target == "/rewards/user?date=2024%2B%2F%3D&signature_type=0&next_cursor=MA%3D%3D" &&
                         requests[4].target == "/rewards/user?date=2024%2B%2F%3D&signature_type=0&next_cursor=N%2B%2F%3D",
                     "user earnings pagination mismatch") &&
               check(requests[5].target == "/rewards/user/total?date=2024%2B%2F%3D&signature_type=0",
                     "total earnings route mismatch") &&
               check(requests[3].headers.at("poly_signature") ==
                         expected_signature(requests[3], "/rewards/user"),
                     "user earnings must sign bare endpoint") &&
               check(requests[4].headers.at("poly_signature") ==
                         expected_signature(requests[4], "/rewards/user"),
                     "each earnings page must sign bare endpoint") &&
               check(requests[3].headers.at("poly_timestamp") !=
                         requests[4].headers.at("poly_timestamp"),
                     "earnings must regenerate L2 headers per page") &&
               check(requests[5].headers.at("poly_signature") ==
                         expected_signature(requests[5], "/rewards/user/total"),
                     "total earnings must sign bare endpoint") &&
               check(current.size() == 2 && current[1].condition_id == "c2" &&
                         current[0].rewards_min_size == "20" &&
                         current[0].rewards_config[0].id == "7" &&
                         current[1].rewards_config[0].rate_per_day == "3.5",
                     "current rewards response mismatch") &&
               check(raw.size() == 1 && raw[0].condition_id == "cond" &&
                         raw[0].question == "Question" && raw[0].market_slug == "market" &&
                         raw[0].event_slug == "event" && raw[0].image == "image-url" &&
                         raw[0].market_competitiveness == "4.25" &&
                         raw[0].tokens.size() == 2 && raw[0].tokens[0].price == "0.6" &&
                         raw[0].rewards_config[0].total_days == "173279",
                     "raw rewards response mismatch") &&
               check(earnings.size() == 2 && earnings[1].condition_id == "c2" &&
                         earnings[0].asset_address == "0xasset1" &&
                         earnings[0].maker_address == "0xmaker" &&
                         earnings[0].earnings == "1.25" && earnings[1].asset_rate == "3.5",
                     "user earnings response mismatch") &&
               check(totals.size() == 1 && totals[0].date == "2024-01-01" &&
                         totals[0].condition_id.empty() &&
                         totals[0].asset_address == "0xasset3" &&
                         totals[0].maker_address == "0xmaker" &&
                         totals[0].earnings == "3.75" && totals[0].asset_rate == "4",
                     "total earnings response mismatch");
    }

    bool test_rewards_schemas_fail_closed_atomically()
    {
        LocalServer server;
        ClobClient public_client(server.url(), 137);
        server.enqueue(R"({"next_cursor":"LTE=","data":[{"condition_id":"current","rewards_config":[{"asset_address":"0xasset","start_date":"2026-07-31","end_date":"2500-12-31","rate_per_day":2,"total_rewards":0}],"rewards_max_spread":5.5,"rewards_min_size":20},{"condition_id":"mistyped","rewards_config":{},"rewards_max_spread":5.5,"rewards_min_size":20}]})");
        const auto current = public_client.get_rewards_markets_current();

        server.enqueue(R"({"next_cursor":"LTE=","data":[{"condition_id":"raw","question":"Question","market_slug":"market","event_slug":"event","image":"","rewards_max_spread":3.5,"rewards_min_size":10,"market_competitiveness":4.25,"tokens":[{"token_id":"1","outcome":"Yes","price":0.6},{"token_id":"2","outcome":"No","price":0.4}],"rewards_config":[{"id":"7","asset_address":"0xasset","start_date":"2026-07-31","end_date":"2500-12-31","rate_per_day":2,"total_rewards":0,"total_days":173279}]},{"condition_id":"mistyped","question":42,"market_slug":"market","event_slug":"event","image":"","rewards_max_spread":3.5,"rewards_min_size":10,"market_competitiveness":4.25,"tokens":[],"rewards_config":[]}]})");
        const auto raw = public_client.get_rewards_markets("raw");

        auto secure_client = authenticated_client(server.url());
        server.enqueue(R"({"next_cursor":"LTE=","data":[{"date":"2026-07-31","condition_id":"condition","asset_address":"0xasset","maker_address":"0xmaker","earnings":1.25,"asset_rate":2},{"date":"2026-07-31","condition_id":"condition","asset_address":"0xasset","maker_address":"0xmaker","earnings":1.25,"asset_rate":true}]})");
        const auto earnings = secure_client.get_earnings_for_user_for_day_all("2026-07-31");

        server.enqueue(R"([{"date":"2026-07-31","asset_address":"0xasset","maker_address":"0xmaker","earnings":3.75,"asset_rate":2},{"date":"2026-07-31","asset_address":"0xasset","maker_address":{},"earnings":3.75,"asset_rate":2}])");
        const auto totals = secure_client.get_total_earnings_for_user_for_day_all("2026-07-31");

        return check(current.empty(),
                     "current rewards must reject a mistyped item atomically") &&
               check(raw.empty(),
                     "raw rewards must reject mistyped market metadata atomically") &&
               check(earnings.empty(),
                     "user earnings must reject a mistyped asset rate atomically") &&
               check(totals.empty(),
                     "total earnings must reject a mistyped maker address atomically");
    }

    bool test_scoring_and_notifications()
    {
        LocalServer server;
        auto client = authenticated_client(server.url());
        server.enqueue(R"({"scoring":true})");
        const auto single = client.is_order_scoring("order+/=");
        server.enqueue(R"({})");
        const auto missing_single = client.is_order_scoring("missing");
        server.enqueue(R"({"scoring":"true"})");
        const auto mistyped_single = client.is_order_scoring("mistyped");

        server.enqueue(R"({"a":true,"b":false})");
        const auto batch = client.are_orders_scoring(
            std::vector<std::string>{"a", "b"});
        server.enqueue(R"({"a":true})");
        const auto missing_batch = client.are_orders_scoring(
            std::vector<std::string>{"a", "b"});
        server.enqueue(R"({"a":true,"b":"false"})");
        const auto mistyped_batch = client.are_orders_scoring(
            std::vector<std::string>{"a", "b"});

        server.enqueue(R"([{"type":2,"owner":"owner","payload":{"order_id":"x"}}])");
        const auto notifications = client.get_notifications();
        server.enqueue(R"([{"type":2,"owner":"owner","payload":{"order_id":"x"}},{"type":3,"payload":{}}])");
        const auto malformed_notifications = client.get_notifications();
        const auto requests = server.requests();

        return check(requests.size() == 8, "expected eight scoring/notification requests") &&
               check(requests[0].target == "/order-scoring?order_id=order%2B%2F%3D",
                     "single scoring wire contract mismatch") &&
               check(requests[3].method == "POST" && requests[3].target == "/orders-scoring" &&
                         requests[3].body == R"(["a","b"])",
                     "batch scoring wire contract mismatch") &&
               check(requests[6].target == "/notifications?signature_type=0",
                     "notifications query mismatch") &&
               check(requests[0].headers.at("poly_signature") ==
                         expected_signature(requests[0], "/order-scoring"),
                     "single scoring must sign bare endpoint") &&
               check(requests[3].headers.at("poly_signature") ==
                         expected_signature(requests[3], "/orders-scoring", requests[3].body),
                     "batch scoring signed body mismatch") &&
               check(requests[6].headers.at("poly_signature") ==
                         expected_signature(requests[6], "/notifications"),
                     "notifications must sign bare endpoint") &&
               check(single && single->scoring && !missing_single && !mistyped_single,
                     "single scoring must reject malformed responses") &&
               check(batch.size() == 2 && batch[0].scoring && !batch[1].scoring &&
                         missing_batch.empty() && mistyped_batch.empty(),
                     "batch scoring must parse atomically") &&
               check(notifications.size() == 1 && notifications[0].type == 2 &&
                         notifications[0].owner == "owner" &&
                         notifications[0].payload == nlohmann::json{{"order_id", "x"}} &&
                         malformed_notifications.empty(),
                     "notifications must preserve the V2 shape atomically");
    }
}
