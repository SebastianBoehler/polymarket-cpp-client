#include "user_stream_test_support.hpp"
#include "user_stream_protocol.hpp"
#include "websocket_test_server.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

using namespace polymarket;
using namespace std::chrono_literals;
using namespace user_stream_test;
using json = nlohmann::json;

namespace user_stream_test
{
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
}
