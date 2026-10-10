#include "../src/clob_client_test_fixture.hpp"
#include "../src/user_stream_protocol.hpp"
#include "check_support.hpp"
#include "polymarket/trade_status_tracker.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <stdexcept>
#include <string>
#include <thread>
#include <variant>
#include <vector>

using namespace polymarket;
using check_support::check;
using namespace std::chrono_literals;

namespace
{
    std::string trade_page(const std::string &id, const std::string &status,
                           const nlohmann::json &hash = "")
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

    UserTradeEvent trade_event(const std::string &id, const std::string &status,
                               const std::string &hash = "")
    {
        UserTradeEvent event;
        event.id = id;
        event.status = status;
        event.transaction_hash = hash;
        return event;
    }

    OrderResponse matched_order(std::vector<std::string> trade_ids)
    {
        OrderResponse order;
        order.success = true;
        order.order_id = "order-1";
        order.status = "matched";
        order.trade_ids = std::move(trade_ids);
        return order;
    }

    void test_stream_confirmation_needs_no_rest()
    {
        clob_test::LocalServer server;
        auto client = clob_test::authenticated_client(server.url());
        TradeStatusTracker tracker;
        tracker.record(trade_event("t1", "MATCHED"));
        std::thread stream(
            [&]
            {
                std::this_thread::sleep_for(50ms);
                tracker.record(trade_event("t1", "TRADE_STATUS_CONFIRMED", "0x11"));
            });
        const auto started = std::chrono::steady_clock::now();
        const auto settled =
            client.wait_for_order_fill_settlement(matched_order({"t1"}), tracker, 5s, 10s);
        const auto elapsed = std::chrono::steady_clock::now() - started;
        stream.join();

        check(settled && settled.value().transaction_hashes == std::vector<std::string>{"0x11"},
              "a fill the stream confirms must settle with its hash");
        check(server.requests().empty() && elapsed < 2s,
              "a stream confirmation must settle without REST lookups");
    }

    void test_final_status_is_sticky()
    {
        TradeStatusTracker tracker;
        tracker.record(trade_event("t1", "CONFIRMED", "0x11"));
        tracker.record(trade_event("t1", "MINED", "0x99"));
        const auto latest = tracker.latest("t1");
        check(latest && latest->status == "CONFIRMED" && latest->transaction_hash == "0x11",
              "a late earlier-stage event must not replace a final status");
    }

    void test_sparse_stream_confirmation_resolves_final_hash()
    {
        for (const auto &hash_fields :
             {nlohmann::json::object(), nlohmann::json{{"transaction_hash", nullptr}},
              nlohmann::json{{"transaction_hash", ""}}})
        {
            clob_test::LocalServer server;
            auto client = clob_test::authenticated_client(server.url());
            TradeStatusTracker tracker;
            tracker.record(trade_event("t1", "MINED", "0xold"));
            auto payload =
                nlohmann::json::parse(trade_page("t1", "TRADE_STATUS_CONFIRMED")).at("data").at(0);
            payload["event_type"] = "trade";
            payload.erase("transaction_hash");
            payload.update(hash_fields);
            const auto events = detail::parse_user_events(payload.dump());
            tracker.record(std::get<UserTradeEvent>(events.at(0)));
            server.enqueue(trade_page("t1", "CONFIRMED", "0xfinal"));
            auto order = matched_order({"t1"});
            order.transaction_hashes = {"0xold"};
            const auto started = std::chrono::steady_clock::now();
            const auto settled = client.wait_for_order_fill_settlement(order, tracker, 5s, 10s);
            check(settled &&
                      settled.value().transaction_hashes == std::vector<std::string>{"0xfinal"} &&
                      settled.value().trades.at(0).transaction_hash == "0xfinal",
                  "sparse confirmations must resolve the final hash instead of reusing MINED");
            check(server.requests().size() == 1 && std::chrono::steady_clock::now() - started < 2s,
                  "a sparse confirmation must reconcile immediately, before the safety interval");
        }
    }

    void test_rest_confirmation_without_hash_keeps_waiting()
    {
        for (const bool use_stream : {false, true})
        {
            clob_test::LocalServer server;
            auto client = clob_test::authenticated_client(server.url());
            TradeStatusTracker tracker;
            tracker.record(trade_event("t1", "CONFIRMED"));
            server.enqueue(trade_page("t1", "CONFIRMED", nullptr));
            server.enqueue(trade_page("t1", "CONFIRMED", "0xfinal"));
            const auto order = matched_order({"t1"});
            const auto settled =
                use_stream ? client.wait_for_order_fill_settlement(order, tracker, 5s, 20ms)
                           : client.wait_for_order_fill_settlement(order, 5s, 20ms);
            check(settled &&
                      settled.value().transaction_hashes == std::vector<std::string>{"0xfinal"},
                  "a confirmed REST trade without a hash must not complete either wait");
            check(server.requests().size() == 2, "only pending hash details must be reconciled");
        }
    }

    void test_missing_hash_timeout_and_lookup_error()
    {
        clob_test::LocalServer server;
        auto client = clob_test::authenticated_client(server.url());
        TradeStatusTracker tracker;
        tracker.record(trade_event("t1", "CONFIRMED"));
        server.enqueue(trade_page("t1", "CONFIRMED", nullptr));
        const auto order = matched_order({"t1"});
        const auto timeout = client.wait_for_order_fill_settlement(order, tracker, 0ms, 10s);
        check(!timeout && timeout.error().code == SdkErrorCode::Timeout &&
                  timeout.error().retryable,
              "missing final hashes must time out rather than return empty success");
        server.enqueue(R"({"error":"unavailable"})", 500);
        const auto failed = client.wait_for_order_fill_settlement(order, tracker, 5s, 10s);
        check(!failed && failed.error().http_status == 500 &&
                  failed.error().endpoint == "/data/trades",
              "hash reconciliation must preserve the original REST error");
        check(server.requests().size() == 2, "each incomplete confirmation must be checked once");
    }

    void test_recovery_reconciles_through_rest()
    {
        clob_test::LocalServer server;
        auto client = clob_test::authenticated_client(server.url());
        TradeStatusTracker tracker;
        server.enqueue(trade_page("t1", "CONFIRMED", "0x11"));
        std::thread stream(
            [&]
            {
                std::this_thread::sleep_for(50ms);
                tracker.mark_recovered();
            });
        const auto started = std::chrono::steady_clock::now();
        const auto settled =
            client.wait_for_order_fill_settlement(matched_order({"t1"}), tracker, 5s, 10s);
        const auto elapsed = std::chrono::steady_clock::now() - started;
        stream.join();

        check(settled.ok(), "a fill confirmed during a gap must settle after recovery");
        check(server.requests().size() == 1 && elapsed < 2s,
              "a recovery must reconcile pending fills through REST at once");
    }

    void test_safety_net_reconciles_without_events()
    {
        clob_test::LocalServer server;
        auto client = clob_test::authenticated_client(server.url());
        TradeStatusTracker tracker;
        server.enqueue(trade_page("t1", "MATCHED"));
        server.enqueue(trade_page("t1", "CONFIRMED", "0x11"));
        const auto settled =
            client.wait_for_order_fill_settlement(matched_order({"t1"}), tracker, 5s, 20ms);

        check(settled.ok() && server.requests().size() == 2,
              "fills the stream never reports must settle through periodic REST lookups");
    }

    void test_timeout_keeps_final_check()
    {
        clob_test::LocalServer server;
        auto client = clob_test::authenticated_client(server.url());
        TradeStatusTracker tracker;
        server.enqueue(trade_page("t1", "MATCHED"));
        const auto pending =
            client.wait_for_order_fill_settlement(matched_order({"t1"}), tracker, 0ms, 10s);

        check(!pending && pending.error().code == SdkErrorCode::Timeout &&
                  pending.error().retryable,
              "unsettled fills must time out as retryable");
        check(server.requests().size() == 1, "a timeout must keep a final REST check");
    }

    void test_failed_fills_and_invalid_arguments()
    {
        clob_test::LocalServer server;
        auto client = clob_test::authenticated_client(server.url());
        TradeStatusTracker tracker;
        tracker.record(trade_event("t3", "FAILED"));
        const auto failed =
            client.wait_for_order_fill_settlement(matched_order({"t3"}), tracker, 5s, 10s);
        const auto invalid =
            client.wait_for_order_fill_settlement(matched_order({"t3"}), tracker, 5s, 0ms);

        check(!failed && failed.error().code == SdkErrorCode::TransactionFailed,
              "fills the stream reports failed must fail the settlement");
        check(!invalid && invalid.error().code == SdkErrorCode::InvalidArgument,
              "a non-positive reconcile interval must be rejected");
        check(server.requests().empty(), "stream-settled and invalid waits must not use REST");
    }

    void test_capacity_forgets_oldest()
    {
        TradeStatusTracker tracker(2);
        tracker.record(trade_event("a", "MATCHED"));
        tracker.record(trade_event("b", "MATCHED"));
        tracker.record(trade_event("c", "MATCHED"));
        check(!tracker.latest("a") && tracker.latest("b") && tracker.latest("c"),
              "the tracker must forget its oldest trade at capacity");

        bool rejected = false;
        try
        {
            TradeStatusTracker empty(0);
        }
        catch (const std::invalid_argument &)
        {
            rejected = true;
        }
        check(rejected, "a zero capacity must be rejected");
    }
} // namespace

int main()
{
    test_stream_confirmation_needs_no_rest();
    test_final_status_is_sticky();
    test_sparse_stream_confirmation_resolves_final_hash();
    test_rest_confirmation_without_hash_keeps_waiting();
    test_missing_hash_timeout_and_lookup_error();
    test_recovery_reconciles_through_rest();
    test_safety_net_reconciles_without_events();
    test_timeout_keeps_final_check();
    test_failed_fills_and_invalid_arguments();
    test_capacity_forgets_oldest();
    return check_support::finish("test_stream_settlement");
}
