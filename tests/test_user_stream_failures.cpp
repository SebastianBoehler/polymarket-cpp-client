#include "user_stream_test_support.hpp"
#include "user_stream_protocol.hpp"
#include "websocket_test_server.hpp"

#include <nlohmann/json.hpp>

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace polymarket;
using namespace std::chrono_literals;
using namespace user_stream_test;
using json = nlohmann::json;

namespace user_stream_test
{
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

    // Rejection is terminal: run() must return whether it was already
    // blocking when the server closed with 1008 or is entered afterwards.
    bool authentication_failure_tests(bool reject_during_run)
    {
        websocket_test::LocalWebSocketServer server;
        Config config;
        config.clob_user_ws_url = server.url();
        config.ws_connect_timeout_ms = 1'000;
        const auto credentials = test_credentials();

        std::mutex errors_mutex;
        std::vector<std::string> errors;
        std::atomic<bool> run_returned{false};
        std::thread runner;
        bool passed = false;
        {
            UserStream stream(config, credentials);
            stream.on_error([&](const std::string &error)
                            {
                                std::lock_guard lock(errors_mutex);
                                errors.push_back(error);
                            });
            stream.subscribe_all_markets();
            const auto start_run = [&]
            {
                runner = std::thread([&]
                                     {
                                         stream.run();
                                         run_returned = true;
                                     });
            };
            passed =
                check(stream.connect(), "user stream connects before auth reply") &&
                check(server.wait_for_message_count(
                          detail::user_subscription_message({true, {}}, credentials),
                          1, 2s),
                      "subscription is sent before rejection");
            if (passed && reject_during_run)
            {
                start_run();
                // Let run() enter its blocking loop before the rejection.
                passed = check(!wait_until([&]
                                           { return run_returned.load(); }, 300ms),
                               "run() blocks while the stream is live");
            }
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
                          "authentication failure is observable");
            }
            if (passed && !reject_during_run) start_run();
            if (passed)
            {
                passed =
                    check(wait_until([&]
                                     { return run_returned.load(); }),
                          reject_during_run ? "rejection ends a blocked run()"
                                            : "run() returns after an earlier rejection") &&
                    check(!server.wait_for_connections(2, 1s),
                          "rejected stream does not reconnect") &&
                    check(!stream.is_connected(), "rejected stream is disconnected");
            }
            // Unblocks run() if the rejection failed to end it.
            stream.stop();
            if (runner.joinable()) runner.join();
        }
        return passed;
    }
}
