#include "polymarket/sports_stream.hpp"
#include "sports_fixtures.hpp"
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

    polymarket::Config local_config(const std::string &url)
    {
        polymarket::Config config;
        config.sports_ws_url = url;
        return config;
    }

    bool test_stream_delivery()
    {
        LocalWebSocketServer server;
        polymarket::SportsStream stream(local_config(server.url()));
        std::mutex mutex;
        std::vector<polymarket::SportsGameResult> results;
        std::atomic<int> gaps{0};
        std::atomic<int> recoveries{0};
        stream.on_result(
            [&](const polymarket::SportsGameResult &result)
            {
                std::lock_guard lock(mutex);
                results.push_back(result);
            });
        stream.on_stream_gap([&] { gaps++; });
        stream.on_stream_recovered([&] { recoveries++; });

        const bool connected = stream.connect();
        const bool first_recovery = wait_until([&] { return recoveries.load() == 1; }, 2s);
        server.send_to_clients("ping");
        server.send_to_clients(sports_fixtures::sports_live_result);
        const bool ponged = server.wait_for_message_count("pong", 1, 2s);
        const bool delivered = wait_until(
            [&]
            {
                std::lock_guard lock(mutex);
                return results.size() == 1;
            },
            2s);

        server.close_clients();
        const bool reconnected = server.wait_for_connections(2, 5s);
        const bool second_recovery = wait_until([&] { return recoveries.load() == 2; }, 5s);
        server.send_to_clients(sports_fixtures::sports_camel_status_result);
        const bool after_reconnect = wait_until(
            [&]
            {
                std::lock_guard lock(mutex);
                return results.size() == 2;
            },
            2s);
        const bool no_client_ping = !server.wait_for_message_count("PING", 1, 100ms);
        stream.disconnect();

        std::lock_guard lock(mutex);
        return check(connected && first_recovery, "a connect must report recovery") &&
               check(ponged, "the stream must answer the server heartbeat") &&
               check(delivered && results[0].game_id == 70918912 && results[0].score == "10-21",
                     "a game update must be delivered") &&
               check(stream.dropped_events() == 0 && stream.result_events() == 2,
                     "valid updates must not be dropped") &&
               check(reconnected && second_recovery && gaps.load() >= 3,
                     "a dropped connection must report a gap and recover") &&
               check(after_reconnect && results[1].league_abbreviation == "mls",
                     "updates must resume after a reconnect") &&
               check(no_client_ping, "the stream must not send client pings");
    }

    bool test_malformed_update_reports_gap()
    {
        LocalWebSocketServer server;
        polymarket::SportsStream stream(local_config(server.url()));
        std::mutex mutex;
        std::vector<std::string> errors;
        std::atomic<int> results{0};
        std::atomic<int> gaps{0};
        std::atomic<int> recoveries{0};
        stream.on_result([&](const polymarket::SportsGameResult &) { results++; });
        stream.on_error(
            [&](const std::string &error)
            {
                std::lock_guard lock(mutex);
                errors.push_back(error);
            });
        stream.on_stream_gap([&] { gaps++; });
        stream.on_stream_recovered([&] { recoveries++; });

        const bool connected = stream.connect();
        const bool first_recovery = wait_until([&] { return recoveries.load() == 1; }, 2s);
        const int gaps_before = gaps.load();
        server.send_to_clients(sports_fixtures::sports_partial_result);
        const bool reconnected = server.wait_for_connections(2, 5s);
        const bool recovered = wait_until([&] { return recoveries.load() == 2; }, 5s);
        server.send_to_clients(sports_fixtures::sports_live_result);
        const bool resumed = wait_until([&] { return results.load() == 1; }, 2s);
        stream.disconnect();

        std::lock_guard lock(mutex);
        const bool reported = !errors.empty() &&
                              errors[0].find("invalid Sports update") != std::string::npos &&
                              errors[0].find("gameId") != std::string::npos;
        return check(connected && first_recovery, "the stream must connect") &&
               check(stream.dropped_events() == 1, "a malformed update must be counted") &&
               check(reported, "a malformed update must be reported with its cause") &&
               check(gaps.load() > gaps_before && reconnected && recovered,
                     "a malformed update must report a gap and recover, got gaps=" +
                         std::to_string(gaps.load() - gaps_before) +
                         " recoveries=" + std::to_string(recoveries.load())) &&
               check(resumed, "updates must resume after recovery");
    }

    bool test_disconnect_stops_delivery()
    {
        LocalWebSocketServer server;
        polymarket::SportsStream stream(local_config(server.url()));
        std::atomic<int> results{0};
        stream.on_result([&](const polymarket::SportsGameResult &) { results++; });
        const bool connected = stream.connect();
        stream.disconnect();
        const bool disconnected = !stream.is_connected();
        const bool reconnected = stream.connect();
        server.send_to_clients(sports_fixtures::sports_live_result);
        const bool delivered = wait_until([&] { return results.load() == 1; }, 2s);
        stream.disconnect();
        return check(connected && disconnected && reconnected && delivered,
                     "a stream must connect again after disconnect");
    }

    bool test_invalid_config()
    {
        polymarket::Config empty;
        empty.sports_ws_url.clear();
        polymarket::Config zero_timeout;
        zero_timeout.ws_connect_timeout_ms = 0;
        int rejected = 0;
        for (const auto &config : {empty, zero_timeout})
        {
            try
            {
                polymarket::SportsStream stream(config);
            }
            catch (const std::invalid_argument &)
            {
                rejected++;
            }
        }
        return check(rejected == 2, "an empty URL or zero timeout must be rejected");
    }

    bool test_unreachable_server()
    {
        polymarket::Config config = local_config("ws://127.0.0.1:1");
        config.ws_connect_timeout_ms = 300;
        polymarket::SportsStream stream(config);
        return check(!stream.connect() && !stream.is_connected(),
                     "an unreachable server must fail to connect");
    }
} // namespace

int main()
{
    const bool passed = test_stream_delivery() && test_malformed_update_reports_gap() &&
                        test_disconnect_stops_delivery() && test_invalid_config() &&
                        test_unreachable_server();
    if (!passed) return 1;
    std::cout << "test_sports_stream passed\n";
    return 0;
}
