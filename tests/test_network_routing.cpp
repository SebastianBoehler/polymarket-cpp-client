#include "check_support.hpp"
#include "http_client_transport_fixture.hpp"
#include "network_proxy_fixture.hpp"
#include "websocket_test_server.hpp"

#include "polymarket/clob_client.hpp"
#include "polymarket/geoblock.hpp"
#include "polymarket/http_client.hpp"
#include "polymarket/network.hpp"
#include "polymarket/websocket_client.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <vector>

using namespace std::chrono_literals;
using network_test::LocalProxy;

namespace
{
    // The HTTP fixture defines its own expect(); keep the shared counter here.
    void expect(bool condition, const std::string &message)
    {
        check_support::check(condition, message);
    }

    // Collects messages and errors delivered on the client's worker threads.
    struct WsObserver
    {
        std::mutex mutex;
        std::condition_variable cv;
        std::vector<std::string> messages;
        std::vector<std::string> errors;

        void attach(polymarket::WebSocketClient &client)
        {
            client.on_message(
                [this](const std::string &message)
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    messages.push_back(message);
                    cv.notify_all();
                });
            client.on_error(
                [this](const std::string &error)
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    errors.push_back(error);
                    cv.notify_all();
                });
        }

        bool wait_for_message(const std::string &expected, std::chrono::milliseconds timeout)
        {
            std::unique_lock<std::mutex> lock(mutex);
            return cv.wait_for(lock, timeout,
                               [&]
                               {
                                   return std::find(messages.begin(), messages.end(), expected) !=
                                          messages.end();
                               });
        }

        bool wait_for_error_containing(const std::string &text, std::chrono::milliseconds timeout)
        {
            std::unique_lock<std::mutex> lock(mutex);
            return cv.wait_for(lock, timeout,
                               [&]
                               {
                                   for (const auto &error : errors)
                                       if (error.find(text) != std::string::npos) return true;
                                   return false;
                               });
        }
    };

    polymarket::WebSocketOptions routed_options(const std::string &proxy_url)
    {
        polymarket::WebSocketOptions options;
        options.min_backoff_ms = 20;
        options.max_backoff_ms = 50;
        options.proxy_url = proxy_url;
        return options;
    }

    std::string server_authority(const websocket_test::LocalWebSocketServer &server)
    {
        return server.url().substr(std::string("ws://").size());
    }

    void websocket_through_http_proxy()
    {
        LocalProxy proxy;
        websocket_test::LocalWebSocketServer server;
        WsObserver observer;
        {
            polymarket::WebSocketClient client;
            client.set_url(server.url() + "/ws/market?feed=1");
            client.configure(routed_options(proxy.http_url()));
            observer.attach(client);
            client.connect();
            expect(client.wait_until_connected(5s), "WS over HTTP proxy did not connect");
            expect(proxy.wait_for_targets(1, 2s), "HTTP proxy saw no CONNECT");
            const auto targets = proxy.targets();
            expect(!targets.empty() && targets.front() == server_authority(server),
                   "CONNECT target is not the WebSocket server");

            const auto payloads = proxy.first_payloads();
            expect(!payloads.empty() &&
                       payloads.front().find("GET /ws/market?feed=1 HTTP/1.1") == 0 &&
                       payloads.front().find("Host: " + server_authority(server) + "\r\n") !=
                           std::string::npos,
                   "upgrade request lost its path or remote Host header");

            expect(client.send("ping-through-proxy"), "send through proxy failed");
            expect(server.wait_for_message_count("ping-through-proxy", 1, 2s),
                   "server did not receive the proxied message");
            expect(server.send_to_clients("book-update"), "server send failed");
            expect(observer.wait_for_message("book-update", 2s),
                   "client did not receive the proxied message");

            // IXWebSocket reconnects to the loopback relay, which must dial
            // the proxy again rather than fall back to a direct connection.
            server.close_clients();
            expect(proxy.wait_for_targets(2, 5s), "reconnect did not go through the proxy");
            expect(server.wait_for_connections(2, 5s), "client did not reconnect");
            client.disconnect();
        }
        expect(server.wait_for_no_clients(5s), "proxied client left a server connection open");
    }

    void websocket_through_socks5h()
    {
        LocalProxy proxy;
        websocket_test::LocalWebSocketServer server;
        polymarket::WebSocketClient client;
        // A host name reaches the SOCKS5 proxy unresolved with socks5h://.
        const auto port = server.url().substr(server.url().rfind(':') + 1);
        client.set_url("ws://localhost:" + port);
        client.configure(routed_options(proxy.socks5h_url()));
        client.connect();
        expect(client.wait_until_connected(5s), "WS over SOCKS5 did not connect");
        const auto targets = proxy.targets();
        expect(!targets.empty() && targets.front() == "localhost:" + port,
               "socks5h did not pass the host name to the proxy");
        client.disconnect();
        expect(server.wait_for_no_clients(5s), "SOCKS5 client left a server connection open");
    }

    void default_route_covers_http_and_websocket()
    {
        LocalProxy proxy;
        LocalHttpServer http_server;
        websocket_test::LocalWebSocketServer ws_server;
        polymarket::set_default_network_route({proxy.http_url(), ""});

        polymarket::HttpClient http;
        http.set_base_url("http://127.0.0.1:" + std::to_string(http_server.port()));
        expect(http.get("/routed").ok(), "HTTP request through default route failed");

        polymarket::WebSocketClient ws;
        ws.set_url(ws_server.url());
        ws.connect();
        expect(ws.wait_until_connected(5s), "WS through default route did not connect");
        ws.disconnect();

        const auto targets = proxy.targets();
        expect(targets.size() == 2 &&
                   targets[0] == "127.0.0.1:" + std::to_string(http_server.port()) &&
                   targets[1] == server_authority(ws_server),
               "default route did not send HTTP and WS through the proxy");
        polymarket::set_default_network_route({});
        expect(ws_server.wait_for_no_clients(5s), "default-route client left a connection open");
    }

    void routes_fail_closed()
    {
        websocket_test::LocalWebSocketServer server;
        LocalHttpServer http_server;
        LocalProxy rejecting(LocalProxy::Mode::Reject);
        const std::string dead_proxy = "http://127.0.0.1:1";

        for (const auto &proxy_url : {dead_proxy, rejecting.http_url()})
        {
            WsObserver observer;
            polymarket::WebSocketClient client;
            client.set_url(server.url());
            auto options = routed_options(proxy_url);
            options.reconnect_enabled = false;
            client.configure(options);
            observer.attach(client);
            client.connect();
            expect(!client.wait_until_connected(3s), "WS connected despite a failed proxy");
            expect(observer.wait_for_error_containing("WebSocket tunnel could not reach", 3s),
                   "WS error does not name the routed failure");
            client.disconnect();

            polymarket::HttpClientOptions http_options;
            http_options.proxy_url = proxy_url;
            polymarket::HttpClient http(http_options);
            http.set_base_url("http://127.0.0.1:" + std::to_string(http_server.port()));
            expect(!http.get("/never").ok(), "HTTP succeeded despite a failed proxy");
        }
        expect(!server.wait_for_connections(1, 200ms), "a failed route reached the WS server");
        expect(http_server.requests().empty(), "a failed route reached the HTTP server");

        polymarket::WebSocketClient unbound;
        unbound.set_url(server.url());
        auto options = routed_options("");
        options.interface_name = "polymarket-no-such-if0";
        options.reconnect_enabled = false;
        unbound.configure(options);
        unbound.connect();
        expect(!unbound.wait_until_connected(3s), "WS connected through a missing interface");
        unbound.disconnect();
    }

    void interface_binding_routes_websocket()
    {
        websocket_test::LocalWebSocketServer server;
        polymarket::WebSocketClient client;
        client.set_url(server.url());
        auto options = routed_options("");
        options.interface_name = "127.0.0.1";
        client.configure(options);
        client.connect();
        expect(client.wait_until_connected(5s), "WS bound to 127.0.0.1 did not connect");
        client.disconnect();
        expect(server.wait_for_no_clients(5s), "bound client left a server connection open");
    }

    void disconnect_interrupts_stalled_proxy()
    {
        LocalProxy stalled(LocalProxy::Mode::Stall);
        websocket_test::LocalWebSocketServer server;
        polymarket::WebSocketClient client;
        client.set_url(server.url());
        client.configure(routed_options(stalled.http_url()));
        client.connect();
        expect(stalled.wait_for_targets(1, 3s), "stalled proxy saw no CONNECT");
        const auto started = std::chrono::steady_clock::now();
        client.disconnect();
        expect(std::chrono::steady_clock::now() - started < 3s,
               "disconnect waited for the stalled proxy connect timeout");
    }

    void orders_follow_the_client_route()
    {
        LocalHttpServer server;
        LocalProxy proxy;
        polymarket::HttpClientOptions options;
        options.proxy_url = proxy.http_url();
        const polymarket::ApiCredentials credentials{"test-key", "c2VjcmV0", "test-passphrase"};
        polymarket::ClobClient client(
            "http://127.0.0.1:" + std::to_string(server.port()), 137,
            "0x0000000000000000000000000000000000000000000000000000000000000001", credentials,
            polymarket::SignatureType::EOA, "", options);

        server.set_response_body(R"({"canceled":["order-1"],"not_canceled":{}})");
        const auto routed = client.cancel_order_result("order-1");
        expect(routed.ok() && proxy.targets().size() == 1,
               "order writes bypassed the configured proxy");

        client.set_proxy("http://127.0.0.1:1");
        const auto requests_before = server.requests().size();
        const auto failed = client.cancel_order_result("order-1");
        expect(!failed.ok() && server.requests().size() == requests_before,
               "order writes must fail closed after the proxy changes");
    }

    void geoblock_check_uses_route()
    {
        LocalHttpServer server;
        LocalProxy proxy;
        polymarket::HttpClientOptions options;
        options.proxy_url = proxy.http_url();
        const auto base_url = "http://127.0.0.1:" + std::to_string(server.port());

        server.set_response_body(
            R"({"blocked":false,"ip":"203.0.113.7","country":"DE","region":"BE"})");
        const auto allowed = polymarket::check_geoblock(options, base_url);
        expect(allowed.ok() && !allowed.value().blocked && allowed.value().ip == "203.0.113.7" &&
                   allowed.value().country == "DE" && allowed.value().region == "BE",
               "geoblock status was not parsed");
        expect(proxy.targets().size() == 1, "geoblock check bypassed the configured proxy");
        const auto requests = server.requests();
        expect(!requests.empty() && requests.back().path == "/api/geoblock",
               "geoblock check used the wrong path");

        server.set_response_body(R"({"blocked":true,"country":"US"})");
        const auto blocked = polymarket::check_geoblock(options, base_url);
        expect(blocked.ok() && blocked.value().blocked && blocked.value().country == "US",
               "blocked geoblock status was not reported");

        server.set_response_body(R"({"ok":true})");
        const auto malformed = polymarket::check_geoblock(options, base_url);
        expect(!malformed.ok() && malformed.error().code == polymarket::SdkErrorCode::Parse,
               "geoblock response without a verdict must fail closed");

        polymarket::HttpClient http(options);
        http.set_base_url(base_url);
        server.set_response_body(R"({"blocked":false,"country":"DE"})");
        const auto proxy_targets_before = proxy.targets().size();
        const auto first = polymarket::check_geoblock(http);
        const auto second = polymarket::check_geoblock(http);
        expect(first.ok() && second.ok() && !second.value().blocked &&
                   second.value().country == "DE",
               "geoblock checks on a shared client were not parsed");
        expect(http.get_stats().total_requests == 2,
               "geoblock checks must run on the caller's client");
        expect(proxy.targets().size() > proxy_targets_before,
               "geoblock checks on a shared client bypassed its proxy");
    }
} // namespace

int main()
{
    websocket_through_http_proxy();
    websocket_through_socks5h();
    default_route_covers_http_and_websocket();
    routes_fail_closed();
    interface_binding_routes_websocket();
    disconnect_interrupts_stalled_proxy();
    geoblock_check_uses_route();
    orders_follow_the_client_route();
    return check_support::finish("test_network_routing");
}
