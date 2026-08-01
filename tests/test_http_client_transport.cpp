#include "clob_client.hpp"
#include "http_client.hpp"
#include "http_client_transport_fixture.hpp"
#include "sdk_error.hpp"

#include <chrono>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

using namespace polymarket;

int main()
{
    http_global_init();
    LocalHttpServer server;

    const HttpResponse empty_response{};
    const HttpClient::ConnectionStats empty_stats{};
    bool ok = true;
    ok &= check(empty_response.status_code == 0 && empty_response.elapsed_ms == 0.0,
                "default HTTP response metrics must be initialized");
    ok &= check(empty_stats.total_requests == 0 && !empty_stats.connection_warm,
                "default connection statistics must be initialized");

    HttpClientOptions options;
    options.timeout_ms = 1000;
    options.connect_timeout_ms = 500;
    options.dns_cache_timeout_seconds = 5;
    options.user_agent = "transport-test";

    HttpClient client(options);
    client.set_base_url("http://127.0.0.1:" + std::to_string(server.port()));

    auto response = client.get("/status");
    auto stats = client.get_stats();
    auto metrics = client.get_last_request_metrics();

    ok &= check(response.ok(), "expected local response to be ok");
    ok &= check(response.body == R"({"ok":true})", "unexpected response body");
    ok &= check(stats.total_requests == 1, "expected one request");
    ok &= check(stats.bytes_received == static_cast<long>(response.body.size()), "expected byte count to match body");
    ok &= check(stats.status_counts[200] == 1, "expected one HTTP 200");
    ok &= check(stats.last_latency_ms > 0.0, "expected last latency");
    ok &= check(stats.min_latency_ms > 0.0, "expected min latency");
    ok &= check(stats.max_latency_ms >= stats.min_latency_ms, "expected max >= min");
    ok &= check(metrics.method == "GET", "expected GET metrics");
    ok &= check(metrics.path == "/status", "expected path metrics");
    ok &= check(metrics.status_code == 200, "expected status metrics");
    ok &= check(metrics.bytes_received == static_cast<long>(response.body.size()), "expected metric byte count");

    auto post_response = client.post("/orders", R"({"id":1})");
    auto delete_response = client.del("/orders");
    const auto requests = server.requests();
    ok &= check(post_response.ok() && delete_response.ok(), "expected POST and DELETE responses to be ok");
    ok &= check(requests.size() == 3, "expected the server to record three requests");
    if (requests.size() == 3)
    {
        ok &= check(requests[2].method == "DELETE", "expected the final request to use DELETE");
        ok &= check(requests[2].path == "/orders", "expected the final DELETE path");
        ok &= check(requests[2].body.empty(), "expected an empty DELETE not to reuse the prior POST body");
    }

    const auto requests_before_redirect = server.requests().size();
    const auto redirect = client.get("/redirect", {{"POLY_API_KEY", "must-not-forward"}});
    ok &= check(redirect.status_code == 302, "authenticated redirects must fail closed");
    ok &= check(server.requests().size() == requests_before_redirect + 1,
                "redirect target must not receive custom authentication headers");

    server.truncate_next_response();
    const auto truncated_response = client.get("/truncated");
    ok &= check(truncated_response.status_code == 200 && !truncated_response.error.empty(),
                "truncated HTTP 200 must preserve its transport error");
    ok &= check(!truncated_response.ok(),
                "truncated HTTP 200 must not satisfy the success predicate");

    constexpr const char *private_key =
        "0x0000000000000000000000000000000000000000000000000000000000000001";
    const ApiCredentials credentials{"test-key", "c2VjcmV0", "test-passphrase"};
    ClobClient clob("http://127.0.0.1:" + std::to_string(server.port()),
                    137, private_key, credentials);
    server.truncate_next_response();
    const auto truncated_cancel = clob.cancel_order_result("order-id");
    ok &= check(!truncated_cancel,
                "mutating Result API must not accept a truncated HTTP 200");
    if (!truncated_cancel)
    {
        ok &= check(truncated_cancel.error().code == SdkErrorCode::HttpTransport,
                    "truncated mutation must report an HTTP transport error");
    }

    bool invalid_heartbeat_rejected = false;
    try
    {
        client.start_heartbeat(0);
    }
    catch (const std::invalid_argument &)
    {
        invalid_heartbeat_rejected = true;
    }
    if (!invalid_heartbeat_rejected)
    {
        client.stop_heartbeat();
    }
    ok &= check(invalid_heartbeat_rejected, "zero heartbeat interval must be rejected");

    server.hold_heartbeat_response();
    client.start_heartbeat(1);
    const bool heartbeat_started = server.wait_for_heartbeat(std::chrono::milliseconds(1500));
    ok &= check(heartbeat_started, "expected the heartbeat request to reach the fixture");

    auto foreground = std::async(std::launch::async, [&client]
                                 { return client.get("/foreground"); });
    const auto foreground_start = std::chrono::steady_clock::now();
    const bool foreground_bounded =
        foreground.wait_for(std::chrono::milliseconds(300)) == std::future_status::ready;
    server.release_heartbeat_response();
    const auto foreground_response = foreground.get();
    const auto foreground_latency = std::chrono::steady_clock::now() - foreground_start;
    client.stop_heartbeat();
    ok &= check(foreground_bounded && foreground_latency < std::chrono::milliseconds(300),
                "stalled heartbeat must yield to foreground traffic within 300ms");
    ok &= check(foreground_response.ok(),
                "foreground traffic must remain safe while preempting a heartbeat");

    HttpClient moving_client(options);
    moving_client.set_base_url("http://127.0.0.1:" + std::to_string(server.port()));
    server.hold_heartbeat_response();
    moving_client.start_heartbeat(1);
    ok &= check(server.wait_for_heartbeat(std::chrono::milliseconds(1500)),
                "expected the heartbeat to start before moving the client");
    auto move_result = std::async(std::launch::async, [&moving_client]
                                  { return std::make_unique<HttpClient>(std::move(moving_client)); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    server.release_heartbeat_response();
    auto moved_client = move_result.get();
    const auto moved_response = moved_client->get("/after-move");
    ok &= check(moved_response.ok(), "moved client must retain a usable connection handle");
    ok &= check(moved_client->get_stats().total_requests == 2,
                "move must retain the in-flight heartbeat request statistics");

    http_global_cleanup();
    return ok ? 0 : 1;
}
