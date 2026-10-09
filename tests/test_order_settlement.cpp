#include "../src/clob_client_test_fixture.hpp"
#include "check_support.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <string>
#include <vector>

using namespace polymarket;
using check_support::check;
using namespace std::chrono_literals;

namespace
{
    std::string trade_page(const std::string &id, const std::string &status,
                           const std::string &hash = "")
    {
        nlohmann::json trade = {
            {"id", id},
            {"taker_order_id", "order-1"},
            {"market", "0x000000000000000000000000000000000000000000000000000000006d61726b"},
            {"asset_id", "123"},
            {"side", "BUY"},
            {"size", "10"},
            {"fee_rate_bps", "0"},
            {"price", "0.5"},
            {"status", status},
            {"match_time", "1705322096"},
            {"last_update", "1705322130"},
            {"outcome", "YES"},
            {"bucket_index", 0},
            {"owner", "test-key"},
            {"maker_address", "0x2222222222222222222222222222222222222222"},
            {"transaction_hash", hash},
            {"trader_side", "TAKER"},
        };
        return nlohmann::json{{"data", nlohmann::json::array({trade})}, {"next_cursor", "LTE="}}
            .dump();
    }

    constexpr const char *empty_page = R"({"data":[],"next_cursor":"LTE="})";

    OrderResponse matched_order(std::vector<std::string> trade_ids)
    {
        OrderResponse order;
        order.success = true;
        order.order_id = "order-1";
        order.status = "matched";
        order.trade_ids = std::move(trade_ids);
        return order;
    }

    std::vector<std::string> targets(const std::vector<clob_test::Request> &requests)
    {
        std::vector<std::string> result;
        result.reserve(requests.size());
        for (const auto &request : requests)
            result.push_back(request.target);
        return result;
    }

    void test_orders_without_fills_do_not_poll()
    {
        clob_test::LocalServer server;
        auto client = clob_test::authenticated_client(server.url());
        const auto resting = client.wait_for_order_fill_settlement(matched_order({}));
        auto with_hashes = matched_order({});
        with_hashes.transaction_hashes = {"0xaa", "0xbb", "0xaa"};
        const auto hashed = client.wait_for_order_fill_settlement(with_hashes);

        check(resting && resting.value().transaction_hashes.empty() &&
                  resting.value().trades.empty(),
              "an order with no fills must settle immediately with nothing");
        check(hashed &&
                  hashed.value().transaction_hashes == std::vector<std::string>{"0xaa", "0xbb"},
              "an order with no trade IDs must return its unique hashes");
        check(server.requests().empty(), "orders without trade IDs must not poll");
    }

    void test_polls_until_every_fill_confirms()
    {
        clob_test::LocalServer server;
        auto client = clob_test::authenticated_client(server.url());
        server.enqueue(trade_page("t1", "MATCHED"));
        server.enqueue(trade_page("t2", "CONFIRMED", "0x22"));
        server.enqueue(empty_page);
        server.enqueue(trade_page("t1", "CONFIRMED", "0x11"));
        const auto settled =
            client.wait_for_order_fill_settlement(matched_order({"t1", "t2", "t1"}), 5s, 1ms);
        const auto requests = server.requests();

        check(settled.ok(), "fills that confirm must settle");
        if (settled)
        {
            check(settled.value().transaction_hashes == std::vector<std::string>{"0x11", "0x22"},
                  "hashes must follow trade_ids order");
            check(settled.value().trades.size() == 2 && settled.value().trades[0].id == "t1" &&
                      settled.value().trades[0].status == "CONFIRMED",
                  "settled trades must follow trade_ids order");
        }
        check(targets(requests) ==
                  std::vector<std::string>{"/data/trades?id=t1", "/data/trades?id=t2",
                                           "/data/trades?id=t1", "/data/trades?id=t1"},
              "only unsettled, unique trade IDs must be polled, including unseen ones");
    }

    void test_failed_fills()
    {
        clob_test::LocalServer server;
        auto client = clob_test::authenticated_client(server.url());
        server.enqueue(trade_page("t1", "FAILED", "0x11"));
        server.enqueue(trade_page("t2", "CONFIRMED", "0x22"));
        const auto partial = client.wait_for_order_fill_settlement(matched_order({"t1", "t2"}));
        server.enqueue(trade_page("t3", "FAILED"));
        const auto failed = client.wait_for_order_fill_settlement(matched_order({"t3"}));

        check(partial && partial.value().transaction_hashes == std::vector<std::string>{"0x22"} &&
                  partial.value().trades.size() == 2,
              "failed fills must keep their trade but contribute no hash");
        check(!failed && failed.error().code == SdkErrorCode::TransactionFailed &&
                  failed.error().message.find("t3") != std::string::npos,
              "an order whose every fill failed must report TransactionFailed");
    }

    void test_prefixed_rest_statuses()
    {
        clob_test::LocalServer server;
        auto client = clob_test::authenticated_client(server.url());
        server.enqueue(trade_page("t1", "TRADE_STATUS_MINED"));
        server.enqueue(trade_page("t1", "TRADE_STATUS_CONFIRMED", "0x11"));
        const auto confirmed =
            client.wait_for_order_fill_settlement(matched_order({"t1"}), 5s, 1ms);
        server.enqueue(trade_page("t2", "TRADE_STATUS_FAILED", "0x22"));
        const auto failed = client.wait_for_order_fill_settlement(matched_order({"t2"}), 5s, 1ms);
        server.enqueue(trade_page("t3", "TRADE_STATUS_FAILED", "0x33"));
        server.enqueue(trade_page("t4", "TRADE_STATUS_CONFIRMED", "0x44"));
        const auto mixed =
            client.wait_for_order_fill_settlement(matched_order({"t3", "t4"}), 5s, 1ms);

        check(confirmed &&
                  confirmed.value().transaction_hashes == std::vector<std::string>{"0x11"} &&
                  confirmed.value().trades.size() == 1 &&
                  confirmed.value().trades[0].status == "TRADE_STATUS_CONFIRMED",
              "a prefixed confirmed fill must settle and keep its raw status");
        check(!failed && failed.error().code == SdkErrorCode::TransactionFailed &&
                  failed.error().message.find("t2") != std::string::npos,
              "a prefixed failed fill must report TransactionFailed");
        check(mixed && mixed.value().transaction_hashes == std::vector<std::string>{"0x44"} &&
                  mixed.value().trades.size() == 2,
              "prefixed mixed fills must keep both trades and only the confirmed hash");
        check(server.requests().size() == 5, "prefixed statuses polling request count mismatch");
    }

    void test_deadline()
    {
        clob_test::LocalServer server;
        auto client = clob_test::authenticated_client(server.url());
        server.enqueue(trade_page("t1", "MINED"));
        const auto immediate = client.wait_for_order_fill_settlement(matched_order({"t1"}), 0ms);

        // A timeout shorter than the poll interval still polls once at the deadline.
        server.enqueue(trade_page("t1", "MINED"));
        server.enqueue(trade_page("t1", "MINED"));
        const auto started = std::chrono::steady_clock::now();
        const auto late = client.wait_for_order_fill_settlement(matched_order({"t1"}), 200ms, 10s);
        const auto elapsed = std::chrono::steady_clock::now() - started;

        server.enqueue(trade_page("t1", "MINED"));
        server.enqueue(trade_page("t1", "CONFIRMED", "0x11"));
        const auto at_deadline =
            client.wait_for_order_fill_settlement(matched_order({"t1"}), 200ms, 10s);

        check(!immediate && immediate.error().code == SdkErrorCode::Timeout &&
                  immediate.error().retryable &&
                  immediate.error().message.find("t1") != std::string::npos,
              "a zero timeout must poll once and report the pending trade");
        check(!late && late.error().code == SdkErrorCode::Timeout, "unsettled fills must time out");
        check(elapsed < 5s, "the wait must not sleep a full poll interval past the deadline");
        check(at_deadline &&
                  at_deadline.value().transaction_hashes == std::vector<std::string>{"0x11"},
              "a fill that confirms at the deadline must settle");
        check(server.requests().size() == 5, "deadline polling request count mismatch");
    }

    void test_lookup_failures()
    {
        clob_test::LocalServer server;
        auto client = clob_test::authenticated_client(server.url());
        server.enqueue(R"({"error":"boom"})", 500);
        const auto failed_lookup = client.wait_for_order_fill_settlement(matched_order({"t1"}));
        server.enqueue(R"({"data":"oops","next_cursor":"LTE="})");
        const auto malformed = client.get_trade_result("t1");
        const auto bad_interval =
            client.wait_for_order_fill_settlement(matched_order({"t1"}), 1s, 0ms);

        ClobClient public_client(server.url(), 137);
        const auto unauthenticated =
            public_client.wait_for_order_fill_settlement(matched_order({"t1"}));

        check(!failed_lookup && failed_lookup.error().http_status == 500,
              "a failed trade lookup must stop the wait with its error");
        check(!malformed && malformed.error().code == SdkErrorCode::Parse,
              "a malformed trade page must be a parse error");
        check(!bad_interval && bad_interval.error().code == SdkErrorCode::InvalidArgument,
              "a zero poll interval must be rejected");
        check(!unauthenticated && unauthenticated.error().code == SdkErrorCode::Auth,
              "a public client must not read account trades");
        check(server.requests().size() == 2, "rejected waits must not reach the server");
    }

    void test_get_trade_matches_id()
    {
        clob_test::LocalServer server;
        auto client = clob_test::authenticated_client(server.url());
        server.enqueue(trade_page("other", "CONFIRMED"));
        const auto unrelated = client.get_trade_result("t1");
        server.enqueue(trade_page("a+b/c", "CONFIRMED"));
        const auto encoded = client.get_trade("a+b/c");
        const auto requests = server.requests();

        check(unrelated && !unrelated.value(), "rows for other trade IDs must be ignored");
        check(encoded && encoded->id == "a+b/c", "a matching trade must be returned");
        check(requests.size() == 2 && requests[1].target == "/data/trades?id=a%2Bb%2Fc",
              "trade IDs must be query-encoded");
        if (requests.size() == 2)
            check(requests[1].headers.at("poly_signature") ==
                      clob_test::expected_signature(requests[1], "/data/trades"),
                  "the trade lookup must sign the path without its query");
    }
} // namespace

int main()
{
    test_orders_without_fills_do_not_poll();
    test_polls_until_every_fill_confirms();
    test_failed_fills();
    test_prefixed_rest_statuses();
    test_deadline();
    test_lookup_failures();
    test_get_trade_matches_id();
    return check_support::finish("test_order_settlement");
}
