#pragma once

#include <string>

namespace polymarket
{
    // Outbound route for SDK connections.
    //
    // proxy_url uses libcurl syntax with optional user:password@ credentials:
    // http://, https://, socks4://, socks4a://, socks5:// (local DNS) or
    // socks5h:// (DNS resolved by the proxy). A URL without a scheme is an
    // HTTP proxy.
    //
    // interface_name binds outgoing sockets to a network interface, local IP
    // address, or host name, for example a VPN tunnel interface such as "wg0"
    // or "utun4". libcurl also accepts its "if!name" and "host!name" forms.
    struct NetworkRoute
    {
        std::string proxy_url;
        std::string interface_name;

        bool empty() const { return proxy_url.empty() && interface_name.empty(); }
    };

    // Process-wide fallback for HttpClient and WebSocketClient instances whose
    // own options leave proxy_url or interface_name empty. It also covers SDK
    // components that create their transports internally, such as
    // PositionClient, OrderbookManager, UserStream, MarketFetcher, and the
    // JSON-RPC clients.
    //
    // HttpClient reads the route when it applies options (construction,
    // configure, set_proxy). WebSocketClient reads it on every connect().
    // Set it once at startup, before creating clients.
    //
    // A configured route fails closed: if the proxy or interface is
    // unavailable, connections fail instead of falling back to a direct route.
    void set_default_network_route(const NetworkRoute &route);
    NetworkRoute default_network_route();
} // namespace polymarket
