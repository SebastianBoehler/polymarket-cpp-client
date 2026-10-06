# Networking: proxies, VPN interfaces, and geoblock checks

The SDK can send every connection it makes (REST, market and user WebSockets,
Polygon JSON-RPC, the relayer, and Gamma) through a proxy, or bind it to a specific
network interface such as a VPN tunnel.

## Route all SDK traffic

Set a process-wide route once at startup, before creating clients:

```cpp
#include <polymarket/network.hpp>

polymarket::set_default_network_route({
    .proxy_url = "socks5h://user:pass@127.0.0.1:1080",
});
```

The default route applies to every `HttpClient` and `WebSocketClient` whose own
options leave the field empty. That includes the transports that `PositionClient`,
`OrderbookManager`, `UserStream`, `MarketFetcher`, and the JSON-RPC clients
create internally.

`HttpClient` reads the route when it applies its options (construction,
`configure`, `set_proxy`). `WebSocketClient` reads it on every `connect()`.

## Route a single client

```cpp
polymarket::HttpClientOptions http;
http.proxy_url = "http://user:pass@proxy.internal:3128";
polymarket::ClobClient client("https://clob.polymarket.com", 137, http);

polymarket::WebSocketOptions ws_options;
ws_options.proxy_url = "socks5h://127.0.0.1:1080";
polymarket::WebSocketClient ws;
ws.configure(ws_options);
```

`ClobClient::set_proxy()` still works and applies to its CLOB and Data API
transports.

## Supported proxies

Proxy URLs use libcurl syntax, with optional `user:password@` credentials:

| Scheme       | Proxy type               | DNS resolved by |
| ------------ | ------------------------ | --------------- |
| `http://`    | HTTP proxy with CONNECT  | proxy           |
| `https://`   | HTTPS proxy with CONNECT | proxy           |
| `socks5h://` | SOCKS5                   | proxy           |
| `socks5://`  | SOCKS5                   | local resolver  |
| `socks4a://` | SOCKS4a                  | proxy           |
| `socks4://`  | SOCKS4                   | local resolver  |
| no scheme    | HTTP proxy (`host:port`) | proxy           |

Prefer `socks5h://` over `socks5://` so host names are not resolved locally.

TLS stays end to end. The SDK tunnels through the proxy and verifies the
Polymarket certificate; the proxy only sees encrypted traffic.

## Bind to a VPN interface

`interface_name` binds outgoing sockets to an interface, local IP address, or
host name. Use it to send SDK traffic over a VPN tunnel interface without making
that tunnel the machine's default route:

```cpp
polymarket::set_default_network_route({.interface_name = "wg0"}); // Linux WireGuard
// macOS tunnels are usually utunN; libcurl also accepts "if!name" and "host!name".
```

A system-wide VPN needs no SDK configuration: every connection already follows
the operating system's routing table.

## Failure behavior

A configured route fails closed. If the proxy is unreachable, rejects the
tunnel, or the interface does not exist, requests and WebSocket connections fail.
They never fall back to a direct connection. WebSocket error callbacks name the
routed failure, for example
`WebSocket tunnel could not reach ws-subscriptions-clob.polymarket.com:443: ...`.

With no route configured, REST requests follow libcurl's standard proxy
environment variables (`HTTPS_PROXY`, `ALL_PROXY`, `NO_PROXY`). WebSockets use
only an explicit route, so set `set_default_network_route` when both must
share one path.

## How WebSockets are routed

IXWebSocket, the WebSocket library, cannot use proxies or bind interfaces. When
a route is set, `WebSocketClient` starts a small relay that listens on
`127.0.0.1`. IXWebSocket connects to it in plain `ws://` mode. Each connection is
forwarded over a libcurl `CONNECT_ONLY` connection, which performs the proxy
handshake and the TLS session with the real host. The relay sets the original
`Host` and `Origin` headers, so the server sees the same handshake as a direct
connection.

Automatic reconnects go back through the relay, which opens a fresh routed
connection each time. Without a route, WebSockets connect directly as before
and none of this code runs.

The relay adds one loopback hop and a few microseconds per frame. That is small
next to the proxy hop itself.

## Check order-placement eligibility

Polymarket rejects orders from restricted jurisdictions and asks integrations to
check eligibility before trading. `check_geoblock` queries
[`GET https://polymarket.com/api/geoblock`](https://docs.polymarket.com/api-reference/geoblock)
through a given route:

```cpp
auto status = client.get_geoblock_status(); // uses the client's route
if (!status) {
    std::cerr << status.error().message << "\n";
} else if (status.value().blocked) {
    std::cerr << "Order placement unavailable from " << status.value().country << "\n";
}
```

A response without a boolean `blocked` field is a parse error, never "allowed".

Proxies and VPNs change your network path, not where you are. Polymarket's
[Terms of Use](https://polymarket.com/tos) prohibit using a VPN, proxy, or similar
tool to misrepresent your location or get around its geographic restrictions.
You are responsible for complying with the terms and the laws that apply to you. Legitimate uses of these routing options include
corporate egress proxies, routing through hosting close to Polymarket's
infrastructure, and isolating trading traffic on dedicated interfaces.
