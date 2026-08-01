#pragma once

#include "websocket_client.hpp"
#include "websocket_callback_exception_tests.hpp"
#include "websocket_owner_reset_tests.hpp"
#include "websocket_test_server.hpp"
#include "websocket_transport_callback_tests.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace websocket_test
{
    using namespace std::chrono_literals;

    inline bool report_failure(const std::string &message)
    {
        std::cerr << "failed: " << message << '\n';
        return false;
    }

    template <typename Predicate>
    bool wait_until(Predicate predicate, std::chrono::milliseconds timeout)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (predicate())
            {
                return true;
            }
            std::this_thread::sleep_for(2ms);
        }
        return predicate();
    }

    inline bool run_callback_stop_test(bool sequenced)
    {
        LocalWebSocketServer server;
        polymarket::WebSocketClient client;
        polymarket::WebSocketOptions options;
        options.reconnect_enabled = false;
        client.configure(options);
        client.set_url(server.url());

        std::promise<void> stop_returned;
        auto stopped = stop_returned.get_future();
        std::atomic<bool> callback_entered{false};
        const auto callback = [&](const std::string &)
        {
            callback_entered.store(true);
            client.stop();
            stop_returned.set_value();
        };
        if (sequenced)
        {
            client.on_sequenced_message(
                [&](const std::string &message, uint64_t) { callback(message); });
        }
        else
        {
            client.on_message(callback);
        }

        client.connect();
        const bool connected = server.wait_for_connections(1, 1s);
        const bool sent = connected && server.send_to_clients("stop-from-callback");
        const bool returned = sent && stopped.wait_for(1s) == std::future_status::ready;
        const bool stopped_cleanly = returned && wait_until(
                                                    [&]
                                                    { return client.state() == polymarket::WsState::DISCONNECTED; },
                                                    1s);
        const bool restarted = stopped_cleanly && client.connect() &&
                               server.wait_for_connections(2, 1s);

        // A non-worker call must reap any worker which completed after stopping itself.
        client.stop();
        if (!connected || !sent || !callback_entered.load() || !returned ||
            !stopped_cleanly || !restarted)
        {
            return report_failure(sequenced
                                      ? "sequenced callback stop must return without self-join"
                                      : "message callback stop must return without self-join");
        }
        return true;
    }

    inline bool run_overflow_recovery_test()
    {
        LocalWebSocketServer server;
        polymarket::WebSocketClient client;
        polymarket::WebSocketOptions options;
        options.message_queue_limit = 1;
        options.reconnect_enabled = false;
        options.min_backoff_ms = 1;
        options.max_backoff_ms = 2;
        client.configure(options);
        client.set_url(server.url());
        client.track_subscription("subscribe-market");

        std::mutex mutex;
        std::condition_variable cv;
        bool blocked_callback_entered = false;
        bool release_blocked_callback = false;
        bool blocked_callback_done = false;
        std::vector<std::string> delivered;
        client.on_message([&](const std::string &message)
                          {
            if (message == "block-consumer")
            {
                std::unique_lock<std::mutex> lock(mutex);
                blocked_callback_entered = true;
                cv.notify_all();
                cv.wait(lock, [&] { return release_blocked_callback; });
                blocked_callback_done = true;
                cv.notify_all();
                return;
            }
            std::lock_guard<std::mutex> lock(mutex);
            delivered.push_back(message); });

        client.connect();
        bool ok = server.wait_for_message_count("subscribe-market", 1, 1s) &&
                  server.send_to_clients("block-consumer");
        {
            std::unique_lock<std::mutex> lock(mutex);
            ok = cv.wait_for(lock, 1s, [&] { return blocked_callback_entered; }) && ok;
        }

        ok = server.send_to_clients("stale-delta-1") && ok;
        ok = server.send_to_clients("stale-delta-2") && ok;
        ok = wait_until([&] { return client.stats().dropped_messages >= 1; }, 1s) && ok;
        ok = server.wait_for_connections(2, 2s) && ok;
        ok = server.wait_for_message_count("subscribe-market", 2, 1s) && ok;

        {
            std::lock_guard<std::mutex> lock(mutex);
            release_blocked_callback = true;
            cv.notify_all();
        }
        {
            std::unique_lock<std::mutex> lock(mutex);
            ok = cv.wait_for(lock, 1s, [&] { return blocked_callback_done; }) && ok;
        }

        ok = server.send_to_clients("fresh-snapshot") && ok;
        ok = wait_until([&]
                        {
                            std::lock_guard<std::mutex> lock(mutex);
                            return delivered.size() == 1;
                        },
                        1s) &&
             ok;
        ok = server.send_to_clients("fresh-delta") && ok;
        ok = wait_until([&]
                        {
                            std::lock_guard<std::mutex> lock(mutex);
                            return delivered.size() == 2;
                        },
                        1s) &&
             ok;

        client.stop();
        std::lock_guard<std::mutex> lock(mutex);
        ok = delivered == std::vector<std::string>{"fresh-snapshot", "fresh-delta"} && ok;
        return ok || report_failure(
                         "overflow must reconnect, replay subscriptions, and drop stale deltas");
    }

    inline bool run_live_reconfigure_rejection_test()
    {
        LocalWebSocketServer server;
        polymarket::WebSocketClient client;
        polymarket::WebSocketOptions options;
        options.reconnect_enabled = false;
        client.configure(options);
        client.set_url(server.url());
        bool ok = client.connect() &&
                  server.wait_for_connections(1, 1s) &&
                  client.wait_until_connected(1s);
        const auto rejects = [](auto operation)
        {
            try
            {
                operation();
                return false;
            }
            catch (const std::logic_error &)
            {
                return true;
            }
        };
        options.message_queue_limit = 2;
        const bool rejected = rejects([&] { client.configure(options); }) &&
                              rejects([&] { client.set_url(server.url()); }) &&
                              rejects([&] { client.set_ping_interval_ms(500); }) &&
                              rejects([&] { client.set_auto_reconnect(false); });
        client.stop();
        return (ok && rejected) ||
               report_failure("live WebSocket reconfiguration must be rejected");
    }

    inline bool run_websocket_client_integration_tests()
    {
        return run_overflow_recovery_test() &&
               run_live_reconfigure_rejection_test() &&
               run_callback_stop_test(false) &&
               run_callback_stop_test(true) &&
               run_callback_exception_tests() &&
               run_owner_reset_tests() &&
               run_transport_callback_lifecycle_tests() &&
               run_overlapping_callback_stop_test();
    }
}
