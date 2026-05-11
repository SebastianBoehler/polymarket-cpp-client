#include "websocket_resilience.hpp"

#include <iostream>
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
    if (!expect_true("push first", queue.push("one")) ||
        !expect_true("push second", queue.push("two")) ||
        !expect_true("push overflow", queue.push("three")) ||
        !expect_true("drop count", queue.dropped() == 1) ||
        !expect_true("bounded size", queue.size() == 2))
    {
        return 1;
    }

    auto first = queue.pop();
    auto second = queue.pop();
    if (!expect_true("first available", first.has_value()) ||
        !expect_true("second available", second.has_value()) ||
        !expect_equal("drop oldest policy", *first, "two") ||
        !expect_equal("last kept", *second, "three"))
    {
        return 1;
    }

    const auto typed = detail::parse_typed_message(
        R"({"topic":"clob_market","type":"agg_orderbook","payload":{"asset_id":"token-1","bids":[],"asks":[]}})");
    if (!expect_true("typed parsed", typed.has_value()) ||
        !expect_equal("topic", typed->topic, "clob_market") ||
        !expect_equal("type", typed->type, "agg_orderbook") ||
        !expect_equal("payload asset id", typed->asset_id, "token-1"))
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

    return 0;
}
