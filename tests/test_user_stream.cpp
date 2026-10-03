#include "user_stream_test_support.hpp"
#include "user_stream_protocol.hpp"

#include <nlohmann/json.hpp>

#include <exception>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

using namespace polymarket;
using namespace std::chrono_literals;
using namespace user_stream_test;
using json = nlohmann::json;

namespace user_stream_test
{
    bool protocol_tests()
    {
        const auto credentials = test_credentials();

        const auto filtered = json::parse(detail::user_subscription_message(
            {false, {"cond-1", "cond-2"}}, credentials));
        const auto all = json::parse(
            detail::user_subscription_message({true, {"ignored"}}, credentials));
        if (!check(filtered["type"] == "user", "subscription type is user") ||
            !check(filtered["auth"]["apiKey"] == "key" &&
                       filtered["auth"]["secret"] == "secret" &&
                       filtered["auth"]["passphrase"] == "passphrase",
                   "subscription carries API credentials") ||
            !check(filtered["markets"] == json::array({"cond-1", "cond-2"}),
                   "filtered subscription lists markets") ||
            !check(!all.contains("markets"),
                   "all-markets subscription omits the market filter"))
            return false;

        const auto update = json::parse(
            detail::user_subscription_update_message({"cond-3"}, false));
        if (!check(update["operation"] == "unsubscribe" &&
                       update["markets"] == json::array({"cond-3"}),
                   "update message shape"))
            return false;

        using detail::plan_user_subscription_sync;
        const detail::UserSubscriptionState empty;
        const detail::UserSubscriptionState one{false, {"a"}};
        const detail::UserSubscriptionState two{false, {"a", "b"}};
        const detail::UserSubscriptionState other{false, {"b"}};
        const detail::UserSubscriptionState everything{true, {}};

        const auto first = plan_user_subscription_sync(empty, one, credentials);
        const auto added = plan_user_subscription_sync(one, two, credentials);
        const auto swapped = plan_user_subscription_sync(one, other, credentials);
        const auto removed_last = plan_user_subscription_sync(one, empty, credentials);
        const auto widened = plan_user_subscription_sync(two, everything, credentials);
        const auto narrowed = plan_user_subscription_sync(everything, one, credentials);
        const auto idle = plan_user_subscription_sync(empty, empty, credentials);
        if (!check(first.messages.size() == 1 && !first.reconnect &&
                       json::parse(first.messages[0])["type"] == "user",
                   "first subscription sends the authenticated message") ||
            !check(added.messages ==
                       std::vector<std::string>{
                           detail::user_subscription_update_message({"b"}, true)},
                   "added market sends a subscribe update") ||
            !check(swapped.messages ==
                       std::vector<std::string>{
                           detail::user_subscription_update_message({"b"}, true),
                           detail::user_subscription_update_message({"a"}, false)},
                   "swapped market subscribes before unsubscribing") ||
            !check(removed_last.messages.empty() && removed_last.reconnect,
                   "removing the last market reconnects instead of widening") ||
            !check(widened.messages ==
                       std::vector<std::string>{
                           detail::user_subscription_update_message({"a", "b"}, false)},
                   "widening to all markets clears the market filter") ||
            !check(narrowed.messages ==
                       std::vector<std::string>{
                           detail::user_subscription_update_message({"a"}, true)},
                   "narrowing from all markets subscribes the filter") ||
            !check(idle.messages.empty() && !idle.reconnect,
                   "inactive to inactive is a no-op"))
            return false;

        const auto orders = detail::parse_user_events(order_message);
        if (!check(orders.size() == 1 &&
                       std::holds_alternative<UserOrderEvent>(orders[0]),
                   "order event parses"))
            return false;
        const auto &order = std::get<UserOrderEvent>(orders[0]);
        if (!check(order.id == "0xorder" && order.market == "cond-1" &&
                       order.asset_id == "yes" && order.side == "BUY" &&
                       order.type == "PLACEMENT" && order.status == "LIVE" &&
                       order.associate_trades.empty() &&
                       order.timestamp == "1782753357257",
                   "order fields are normalized"))
            return false;

        const auto trades = detail::parse_user_events(trade_message);
        if (!check(trades.size() == 1 &&
                       std::holds_alternative<UserTradeEvent>(trades[0]),
                   "trade event array parses"))
            return false;
        const auto &trade = std::get<UserTradeEvent>(trades[0]);
        if (!check(trade.status == "MATCHED" && trade.match_time == "1782753357" &&
                       trade.timestamp == "1782753357257" &&
                       trade.trader_side == "MAKER" && trade.bucket_index == 2u &&
                       trade.maker_orders.size() == 1 &&
                       trade.maker_orders[0].asset_id == "yes" &&
                       trade.maker_orders[0].side == "BUY" &&
                       trade.maker_orders[0].maker_address.empty(),
                   "trade fields and maker orders are normalized"))
            return false;

        const auto envelope = detail::parse_user_events(
            R"({"topic":"user","type":"order","payload":{"id":"o","owner":"k","market":"m","token_id":"t","side":"SELL","original_size":"1","size_matched":"1","price":"0.5","type":"CANCELLATION"}})");
        if (!check(envelope.size() == 1 &&
                       std::get<UserOrderEvent>(envelope[0]).type == "CANCELLATION",
                   "normalized envelope parses"))
            return false;

        if (!check(detail::parse_user_events(
                       R"({"event_type":"heartbeat","id":"x"})")
                       .empty(),
                   "unknown event types are ignored"))
            return false;

        for (const auto *invalid : {
                 R"({"event_type":"order","owner":"k","market":"m","asset_id":"t","side":"BUY","original_size":"1","size_matched":"0","price":"0.5","type":"PLACEMENT"})",
                 R"({"event_type":"order","id":"o","owner":"k","market":"m","asset_id":"t","side":"HOLD","original_size":"1","size_matched":"0","price":"0.5","type":"PLACEMENT"})",
                 R"({"event_type":"trade","id":"t","taker_order_id":"o","market":"m","asset_id":"t","side":"BUY","size":"1","price":"0.5","status":"MATCHED","owner":"k","bucket_index":-1})",
                 R"({"event_type":"trade","id":"t","taker_order_id":"o","market":"m","asset_id":"t","side":"BUY","size":"1","price":"0.5","status":"MATCHED","owner":"k","maker_orders":{}})",
                 R"(["not-an-object"])"})
        {
            bool rejected = false;
            try
            {
                (void)detail::parse_user_events(invalid);
            }
            catch (const std::exception &)
            {
                rejected = true;
            }
            if (!check(rejected, "malformed user events are rejected"))
                return false;
        }

        bool rejected_credentials = false;
        try
        {
            UserStream stream(Config{}, ApiCredentials{"key", "", "passphrase"});
        }
        catch (const std::invalid_argument &)
        {
            rejected_credentials = true;
        }
        return check(rejected_credentials, "incomplete credentials are rejected");
    }
}

int main()
{
    if (!protocol_tests()) return 1;
    if (!stream_tests()) return 1;
    if (!recovery_ordering_tests()) return 1;
    if (!delayed_handshake_tests()) return 1;
    if (!authentication_failure_tests(true)) return 1;
    if (!authentication_failure_tests(false)) return 1;
    std::cout << "user stream tests passed\n";
    return 0;
}
