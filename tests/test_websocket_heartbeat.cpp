#include "polymarket/websocket_client.hpp"
#include "websocket_resilience.hpp"
#include "websocket_test_server.hpp"

#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
    using namespace std::chrono_literals;
    using websocket_test::LocalWebSocketServer;

    bool check(bool condition, const std::string &message)
    {
        if (!condition) std::cerr << "failed: " << message << '\n';
        return condition;
    }

    template <typename Predicate>
    bool wait_until(Predicate predicate, std::chrono::milliseconds timeout)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (predicate()) return true;
            std::this_thread::sleep_for(2ms);
        }
        return predicate();
    }

    polymarket::WebSocketOptions quiet_options()
    {
        polymarket::WebSocketOptions options;
        options.ping_message.clear();
        options.min_backoff_ms = 10;
        options.max_backoff_ms = 50;
        return options;
    }

    bool test_timed_pop()
    {
        polymarket::detail::BoundedMessageQueue queue(4);
        const auto empty = queue.pop_for(20ms);
        queue.push({"one", 1});
        const auto first = queue.pop_for(20ms);
        queue.close();
        const auto closed = queue.pop_for(1000ms);
        return check(!empty && first && first->payload == "one" && !closed && queue.closed(),
                     "a timed pop must time out, deliver and stop on close");
    }

    bool test_option_validation()
    {
        polymarket::WebSocketClient client;
        auto options = quiet_options();
        options.server_ping_message = "ping";
        bool rejected = false;
        try
        {
            client.configure(options);
        }
        catch (const std::invalid_argument &)
        {
            rejected = true;
        }
        options.server_ping_message.clear();
        options.server_ping_reply = "pong";
        bool reply_rejected = false;
        try
        {
            client.configure(options);
        }
        catch (const std::invalid_argument &)
        {
            reply_rejected = true;
        }
        return check(rejected && reply_rejected,
                     "a server ping needs both the message and the reply");
    }

    bool test_server_ping_reply()
    {
        LocalWebSocketServer server;
        polymarket::WebSocketClient client;
        auto options = quiet_options();
        options.server_ping_message = "ping";
        options.server_ping_reply = "pong";
        client.configure(options);
        client.set_url(server.url());
        std::mutex mutex;
        std::vector<std::string> delivered;
        client.on_message(
            [&](const std::string &message)
            {
                std::lock_guard<std::mutex> lock(mutex);
                delivered.push_back(message);
            });
        client.connect();
        const bool connected = client.wait_until_connected(2s);
        server.send_to_clients("ping");
        server.send_to_clients("{\"gameId\":1}");
        const bool ponged = server.wait_for_message_count("pong", 1, 2s);
        const bool received = wait_until(
            [&]
            {
                std::lock_guard<std::mutex> lock(mutex);
                return !delivered.empty();
            },
            2s);
        const bool no_client_ping = !server.wait_for_message_count("PING", 1, 1500ms);
        client.disconnect();
        std::lock_guard<std::mutex> lock(mutex);
        return check(connected && ponged, "a server ping must be answered with the reply") &&
               check(received && delivered.size() == 1 && delivered[0] == "{\"gameId\":1}",
                     "a server ping must not be delivered as a message") &&
               check(no_client_ping, "an empty ping message must send no client pings") &&
               check(client.stats().messages_received == 1,
                     "a server ping must not count as a message");
    }

    bool test_custom_ping_message()
    {
        LocalWebSocketServer server;
        polymarket::WebSocketClient client;
        auto options = quiet_options();
        options.ping_message = "hb";
        options.ping_interval_ms = 1000;
        client.configure(options);
        client.set_url(server.url());
        client.connect();
        const bool connected = client.wait_until_connected(2s);
        const bool pinged = server.wait_for_message_count("hb", 1, 3s);
        client.disconnect();
        return check(connected && pinged, "the client must send its configured ping text");
    }

    bool test_idle_timeout_reconnects()
    {
        LocalWebSocketServer server;
        polymarket::WebSocketClient client;
        auto options = quiet_options();
        options.idle_timeout_ms = 200;
        client.configure(options);
        client.set_url(server.url());
        std::atomic<int> gaps{0};
        client.on_stream_gap([&](uint64_t) { gaps++; });
        client.track_subscription("{\"action\":\"subscribe\"}");
        client.connect();
        const bool reconnected = server.wait_for_connections(2, 3s);
        const bool restored = server.wait_for_message_count("{\"action\":\"subscribe\"}", 2, 3s);
        const bool connected_again = client.wait_until_connected(2s);
        const auto stats = client.stats();
        client.disconnect();
        return check(reconnected && restored && connected_again,
                     "an idle connection must reconnect and restore subscriptions") &&
               check(stats.idle_timeouts >= 1 && stats.reconnects >= 1 && gaps.load() >= 2,
                     "an idle timeout must be counted and reported as a gap");
    }

    bool test_activity_keeps_connection()
    {
        LocalWebSocketServer server;
        polymarket::WebSocketClient client;
        auto options = quiet_options();
        options.idle_timeout_ms = 2000;
        options.server_ping_message = "ping";
        options.server_ping_reply = "pong";
        client.configure(options);
        client.set_url(server.url());
        client.connect();
        const bool connected = client.wait_until_connected(2s);
        const auto until = std::chrono::steady_clock::now() + 3s;
        int pings = 0;
        while (std::chrono::steady_clock::now() < until)
        {
            server.send_to_clients("ping");
            pings++;
            std::this_thread::sleep_for(100ms);
        }
        const bool ponged = server.wait_for_message_count("pong", 1, 2s);
        const bool still_one = !server.wait_for_connections(2, 200ms);
        const auto stats = client.stats();
        client.disconnect();
        return check(connected && ponged && still_one && stats.idle_timeouts == 0,
                     "server pings must keep an idle timeout from firing (pings " +
                         std::to_string(pings) + ", reconnects " +
                         std::to_string(stats.reconnects) + ", idle timeouts " +
                         std::to_string(stats.idle_timeouts) + ")");
    }
} // namespace

int main()
{
    const bool passed = test_timed_pop() && test_option_validation() && test_server_ping_reply() &&
                        test_custom_ping_message() && test_idle_timeout_reconnects() &&
                        test_activity_keeps_connection();
    if (!passed) return 1;
    std::cout << "test_websocket_heartbeat passed\n";
    return 0;
}
