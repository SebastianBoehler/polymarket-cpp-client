#include "user_stream.hpp"
#include "user_stream_protocol.hpp"
#include "websocket_test_server.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace polymarket;
using namespace std::chrono_literals;
using json = nlohmann::json;

namespace
{
    bool check(bool value, const char *name)
    {
        if (!value)
        {
            std::cerr << "failed: " << name << '\n';
        }
        return value;
    }

    template <typename Predicate>
    bool wait_until(Predicate predicate, std::chrono::milliseconds timeout = 2s)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (predicate()) return true;
            std::this_thread::sleep_for(2ms);
        }
        return predicate();
    }

    ApiCredentials test_credentials()
    {
        return {"key", "secret", "passphrase"};
    }

    constexpr const char *order_message =
        R"({"event_type":"order","id":"0xorder","owner":"owner-key","market":"cond-1","asset_id":"yes","side":"buy","original_size":"10","size_matched":"0","price":"0.42","type":"PLACEMENT","status":"LIVE","order_type":"GTC","associate_trades":null,"outcome":"Yes","created_at":"1782753357","expiration":"0","timestamp":"1782753357257"})";

    constexpr const char *trade_message =
        R"([{"event_type":"trade","id":"trade-1","taker_order_id":"0xtaker","market":"cond-1","asset_id":"yes","side":"SELL","size":"5","price":"0.42","status":"MATCHED","owner":"owner-key","fee_rate_bps":"0","matchtime":"1782753357","last_update":"1782753358","timestamp":1782753357257,"trader_side":"maker","bucket_index":2,"maker_orders":[{"order_id":"0xorder","owner":"owner-key","matched_amount":"5","price":"0.42","token_id":"yes","side":"buy","outcome":"Yes"}]}])";

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

    bool stream_tests()
    {
        websocket_test::LocalWebSocketServer server;
        Config config;
        config.clob_user_ws_url = server.url();
        config.ws_connect_timeout_ms = 1'000;
        const auto credentials = test_credentials();

        std::atomic<unsigned int> gaps{0};
        std::mutex events_mutex;
        std::vector<UserOrderEvent> orders;
        std::vector<UserTradeEvent> trades;

        bool passed = false;
        {
            UserStream stream(config, credentials);
            stream.on_stream_gap([&gaps]
                                 { ++gaps; });
            stream.on_order([&](const UserOrderEvent &order)
                            {
                                std::lock_guard lock(events_mutex);
                                orders.push_back(order);
                            });
            stream.on_trade([&](const UserTradeEvent &trade)
                            {
                                std::lock_guard lock(events_mutex);
                                trades.push_back(trade);
                            });
            stream.subscribe("cond-1");

            const auto initial = detail::user_subscription_message(
                {false, {"cond-1"}}, credentials);
            passed =
                check(stream.connect(), "user stream connects") &&
                check(server.wait_for_message_count(initial, 1, 2s),
                      "connect sends the tracked authenticated subscription") &&
                check(wait_until([&gaps]
                                 { return gaps.load() >= 1; }),
                      "connect reports a reconciliation gap") &&
                check(server.send_to_clients(order_message) &&
                          server.send_to_clients(trade_message),
                      "server sends user events") &&
                check(wait_until([&]
                                 {
                                     std::lock_guard lock(events_mutex);
                                     return orders.size() == 1 && trades.size() == 1;
                                 }),
                      "order and trade callbacks fire") &&
                check(stream.order_events() == 1 && stream.trade_events() == 1,
                      "event counters advance");
            if (passed)
            {
                stream.subscribe(std::vector<std::string>{"cond-2", "cond-1"});
                stream.unsubscribe("cond-1");
                passed =
                    check(server.wait_for_message_count(
                              detail::user_subscription_update_message({"cond-2"}, true),
                              1, 2s),
                          "subscribe sends an incremental update") &&
                    check(server.wait_for_message_count(
                              detail::user_subscription_update_message({"cond-1"}, false),
                              1, 2s),
                          "unsubscribe sends an incremental update") &&
                    check(stream.subscribed_markets() ==
                              std::vector<std::string>{"cond-2"},
                          "subscribed markets are tracked");
            }
            if (passed)
            {
                const auto gaps_before = gaps.load();
                server.close_clients();
                passed =
                    check(server.wait_for_message_count(
                              detail::user_subscription_message({false, {"cond-2"}},
                                                                credentials),
                              1, 5s),
                          "reconnect replays the current subscription") &&
                    check(wait_until([&]
                                     { return gaps.load() > gaps_before; }),
                          "reconnect reports a reconciliation gap");
            }
            if (passed)
            {
                const auto gaps_before = gaps.load();
                passed =
                    check(wait_until([&stream]
                                     { return stream.is_connected(); }, 5s),
                          "stream is connected before invalid payload") &&
                    check(server.send_to_clients(
                              R"({"event_type":"order","id":"missing-fields"})"),
                          "server sends invalid user event") &&
                    check(wait_until([&]
                                     { return gaps.load() > gaps_before; }),
                          "invalid payload forces a reconcile gap");
            }
            if (passed)
            {
                passed = check(wait_until([&stream]
                                          { return stream.is_connected(); }, 5s),
                               "stream reconnects after invalid payload");
                stream.subscribe_all_markets();
                passed = passed &&
                         check(server.wait_for_message_count(
                                   detail::user_subscription_update_message({"cond-2"}, false),
                                   1, 2s),
                               "all-markets subscription clears the market filter") &&
                         check(stream.is_subscribed_to_all_markets(),
                               "all-markets mode is tracked");
            }
            stream.stop();
        }
        return passed;
    }

    bool recovery_ordering_tests()
    {
        websocket_test::LocalWebSocketServer server;
        Config config;
        config.clob_user_ws_url = server.url();
        config.ws_connect_timeout_ms = 3'000;
        const auto credentials = test_credentials();
        const auto subscription = detail::user_subscription_message(
            {false, {"cond-1"}}, credentials);

        std::mutex log_mutex;
        std::vector<std::string> log;
        std::atomic<unsigned int> recoveries{0};
        bool passed = false;
        {
            UserStream stream(config, credentials);
            stream.on_stream_gap([&]
                                 {
                                     std::lock_guard lock(log_mutex);
                                     log.push_back("gap");
                                 });
            stream.on_stream_recovered(
                [&]
                {
                    // The transport thread blocks here, so this only succeeds
                    // if the subscription for this connection was sent first.
                    const bool subscribed = server.wait_for_message_count(
                        subscription, recoveries.load() + 1, 1s);
                    {
                        std::lock_guard lock(log_mutex);
                        log.push_back(subscribed ? "recovered" : "recovered-early");
                    }
                    ++recoveries;
                });
            stream.subscribe("cond-1");

            passed = check(stream.connect(), "user stream connects") &&
                     check(wait_until([&]
                                      { return recoveries.load() == 1; }),
                           "connect reports recovery");
            if (passed)
            {
                server.close_clients();
                passed = check(wait_until([&]
                                          { return recoveries.load() == 2; }, 5s),
                               "reconnect reports recovery");
            }
            stream.stop();
        }

        std::lock_guard lock(log_mutex);
        if (!passed) return false;
        const auto recovered_entries = std::count(log.begin(), log.end(),
                                                  std::string("recovered"));
        bool gap_before_each_recovery = !log.empty() && log.front() == "gap";
        for (std::size_t i = 1; i < log.size(); ++i)
        {
            if (log[i] == "recovered" && log[i - 1] != "gap")
                gap_before_each_recovery = false;
        }
        return check(recovered_entries == 2 &&
                         std::find(log.begin(), log.end(), "recovered-early") ==
                             log.end(),
                     "recovery fires only after the subscription is restored") &&
               check(gap_before_each_recovery,
                     "gap notification precedes each recovery");
    }

    bool delayed_handshake_tests()
    {
        // The first handshake completes well after connect() gives up.
        websocket_test::LocalWebSocketServer server(1'500ms);
        Config config;
        config.clob_user_ws_url = server.url();
        config.ws_connect_timeout_ms = 300;
        const auto credentials = test_credentials();

        std::atomic<unsigned int> orders{0};
        bool passed = false;
        {
            UserStream stream(config, credentials);
            stream.on_order([&orders](const UserOrderEvent &)
                            { ++orders; });
            stream.subscribe("cond-1");

            passed =
                check(!stream.connect(), "connect times out on a delayed handshake") &&
                check(!wait_until([&stream]
                                  { return stream.is_connected(); }, 2'500ms),
                      "timed-out connect stops the transport");
            if (passed)
            {
                passed =
                    check(stream.connect(), "retry connects after a timed-out attempt") &&
                    check(server.wait_for_message_count(
                              detail::user_subscription_message({false, {"cond-1"}},
                                                                credentials),
                              1, 2s),
                          "retry sends the authenticated subscription") &&
                    check(server.send_to_clients(order_message),
                          "server sends an order after retry") &&
                    check(wait_until([&orders]
                                     { return orders.load() == 1; }),
                          "retried stream delivers events");
            }
            stream.stop();
        }
        return passed;
    }

    bool authentication_failure_tests()
    {
        websocket_test::LocalWebSocketServer server;
        Config config;
        config.clob_user_ws_url = server.url();
        config.ws_connect_timeout_ms = 1'000;
        const auto credentials = test_credentials();

        std::mutex errors_mutex;
        std::vector<std::string> errors;
        bool passed = false;
        {
            UserStream stream(config, credentials);
            stream.on_error([&](const std::string &error)
                            {
                                std::lock_guard lock(errors_mutex);
                                errors.push_back(error);
                            });
            stream.subscribe_all_markets();
            passed =
                check(stream.connect(), "user stream connects before auth reply") &&
                check(server.wait_for_message_count(
                          detail::user_subscription_message({true, {}}, credentials),
                          1, 2s),
                      "subscription is sent before rejection");
            if (passed)
            {
                server.close_clients(1008, "authentication failed");
                passed =
                    check(wait_until([&]
                                     {
                                         std::lock_guard lock(errors_mutex);
                                         return errors.size() == 1;
                                     }),
                          "rejection is reported through on_error") &&
                    check(errors[0].find("authentication failed") != std::string::npos,
                          "rejection error carries the server reason") &&
                    check(stream.authentication_failed(),
                          "authentication failure is observable") &&
                    check(!server.wait_for_connections(2, 1s),
                          "rejected stream does not reconnect") &&
                    check(!stream.is_connected(), "rejected stream is disconnected");
            }
            stream.stop();
        }
        return passed;
    }
}

int main()
{
    if (!protocol_tests()) return 1;
    if (!stream_tests()) return 1;
    if (!recovery_ordering_tests()) return 1;
    if (!delayed_handshake_tests()) return 1;
    if (!authentication_failure_tests()) return 1;
    std::cout << "user stream tests passed\n";
    return 0;
}
