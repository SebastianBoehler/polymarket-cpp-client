#include "websocket_resilience.hpp"
#include "websocket_client_integration_tests.hpp"

#include <chrono>
#include <future>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

using namespace polymarket;

namespace
{
    bool expect_true(const std::string &name, bool value)
    {
        if (value)
        {
            return true;
        }
        std::cerr << "failed: " << name << "\n";
        return false;
    }

    bool expect_equal(const std::string &name, const std::string &actual, const std::string &expected)
    {
        if (actual == expected)
        {
            return true;
        }
        std::cerr << name << " mismatch\n"
                  << "  expected: " << expected << "\n"
                  << "  actual:   " << actual << "\n";
        return false;
    }
}

int main()
{
    detail::BoundedMessageQueue queue(2);
    if (!expect_true("push first", queue.push({"one", 1}) == detail::QueuePushResult::Accepted) ||
        !expect_true("push second", queue.push({"two", 1}) == detail::QueuePushResult::Accepted) ||
        !expect_true("overflow reports a stream gap",
                     queue.push({"three", 1}) == detail::QueuePushResult::Gap) ||
        !expect_true("queued and overflow messages are counted as dropped", queue.dropped() == 3) ||
        !expect_true("gap clears the queue", queue.size() == 0) ||
        !expect_true("post-gap message accepted",
                     queue.push({"three", 2}) == detail::QueuePushResult::Accepted))
    {
        return 1;
    }

    auto first = queue.pop();
    if (!expect_true("first available", first.has_value()) ||
        !expect_equal("only post-gap message remains", first->payload, "three") ||
        !expect_true("post-gap generation retained", first->generation == 2))
    {
        return 1;
    }
    queue.reset(4);
    if (!expect_true("queue reset keeps cumulative drop telemetry",
                     queue.dropped() == 3))
    {
        return 1;
    }

    const auto subscribe = nlohmann::json::parse(
        detail::market_subscription_message({"token-1", "token-2"}));
    if (!expect_true("market subscription asset count", subscribe["assets_ids"].size() == 2) ||
        !expect_equal("market subscription first asset", subscribe["assets_ids"][0], "token-1") ||
        !expect_equal("market subscription type", subscribe["type"], "market") ||
        !expect_true("market subscription enables custom events", subscribe["custom_feature_enabled"]))
    {
        return 1;
    }

    const auto unsubscribe = nlohmann::json::parse(
        detail::market_subscription_update_message({"token-2"}, false));
    if (!expect_equal("unsubscribe operation", unsubscribe["operation"], "unsubscribe") ||
        !expect_equal("unsubscribe asset", unsubscribe["assets_ids"][0], "token-2"))
    {
        return 1;
    }

    const auto typed = detail::parse_typed_message(
        R"({"event_type":"price_change","market":"condition-1","price_changes":[{"asset_id":"token-1","price":"0.42","size":"10","side":"BUY","best_bid":"0.42","best_ask":"0.43"}],"timestamp":"1782753357257"})");
    if (!expect_true("typed parsed", typed.has_value()) ||
        !expect_equal("event type", typed->event_type, "price_change") ||
        !expect_equal("nested price-change asset id", typed->asset_id, "token-1"))
    {
        return 1;
    }

    const auto legacy = detail::parse_typed_message(
        R"({"event_type":"book","asset_id":"token-2","bids":[],"asks":[]})");
    if (!expect_true("legacy parsed", legacy.has_value()) ||
        !expect_equal("legacy event type", legacy->event_type, "book") ||
        !expect_equal("legacy asset id", legacy->asset_id, "token-2"))
    {
        return 1;
    }

    const auto heartbeat = detail::parse_typed_message("{}");
    if (!expect_true("heartbeat ignored", !heartbeat.has_value()))
    {
        return 1;
    }

    bool threw = false;
    try
    {
        (void)detail::parse_typed_message("{not-json");
    }
    catch (...)
    {
        threw = true;
    }
    if (!expect_true("malformed json throws for caller metrics", threw))
    {
        return 1;
    }

    if (!expect_true("unlimited reconnects never reach limit",
                     !detail::reconnect_limit_reached(100, 0)) ||
        !expect_true("retry before configured limit is allowed",
                     !detail::reconnect_limit_reached(2, 3)) ||
        !expect_true("configured retry limit is enforced",
                     detail::reconnect_limit_reached(3, 3)))
    {
        return 1;
    }

    WebSocketClient client;
    client.track_subscription("old");
    client.replace_subscriptions({"new"});
    if (!expect_true("subscription replacement is atomic-sized",
                     client.tracked_subscription_count() == 1))
    {
        return 1;
    }

    const auto rejects_invalid_options = [](auto invalidate)
    {
        WebSocketClient invalid_client;
        WebSocketOptions invalid_options;
        invalidate(invalid_options);
        try
        {
            invalid_client.configure(invalid_options);
            return false;
        }
        catch (const std::invalid_argument &)
        {
            return true;
        }
    };
    WebSocketClient invalid_ping_client;
    bool invalid_ping_rejected = false;
    try
    {
        invalid_ping_client.set_ping_interval_ms(0);
    }
    catch (const std::invalid_argument &)
    {
        invalid_ping_rejected = true;
    }
    if (!expect_true("negative reconnect attempts rejected",
                     rejects_invalid_options([](auto &value)
                                             { value.max_reconnect_attempts = -1; })) ||
        !expect_true("zero reconnect backoff rejected",
                     rejects_invalid_options([](auto &value)
                                             { value.min_backoff_ms = 0; })) ||
        !expect_true("inverted reconnect backoff rejected",
                     rejects_invalid_options([](auto &value)
                                             { value.min_backoff_ms = value.max_backoff_ms + 1; })) ||
        !expect_true("zero message queue rejected",
                     rejects_invalid_options([](auto &value)
                                             { value.message_queue_limit = 0; })) ||
        !expect_true("non-positive ping rejected",
                     rejects_invalid_options([](auto &value)
                                             { value.ping_interval_ms = 0; }) &&
                         invalid_ping_rejected))
    {
        return 1;
    }

    const auto wait_started = std::chrono::steady_clock::now();
    if (!expect_true("bounded connection wait reports timeout",
                     !client.wait_until_connected(std::chrono::milliseconds(5))) ||
        !expect_true("connection wait remains bounded",
                     std::chrono::steady_clock::now() - wait_started < std::chrono::milliseconds(100)))
    {
        return 1;
    }

    WebSocketClient stopped_before_run;
    stopped_before_run.stop();
    auto run_result = std::async(std::launch::async, [&stopped_before_run]
                                 { stopped_before_run.run(); });
    const bool returned = run_result.wait_for(std::chrono::milliseconds(20)) ==
                          std::future_status::ready;
    if (!returned)
    {
        stopped_before_run.stop();
        run_result.wait();
    }
    if (!expect_true("stop before run is not lost", returned))
    {
        return 1;
    }

    if (!websocket_test::run_websocket_client_integration_tests())
    {
        return 1;
    }

    return 0;
}
