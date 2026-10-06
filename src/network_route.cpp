#include "network_route.hpp"

#include <mutex>

namespace polymarket
{
    namespace
    {
        struct DefaultRouteState
        {
            std::mutex mutex;
            NetworkRoute route;
        };

        DefaultRouteState &default_route_state()
        {
            static DefaultRouteState state;
            return state;
        }
    } // namespace

    void set_default_network_route(const NetworkRoute &route)
    {
        auto &state = default_route_state();
        std::lock_guard<std::mutex> lock(state.mutex);
        state.route = route;
    }

    NetworkRoute default_network_route()
    {
        auto &state = default_route_state();
        std::lock_guard<std::mutex> lock(state.mutex);
        return state.route;
    }

    namespace detail
    {
        NetworkRoute resolve_network_route(const std::string &proxy_url,
                                           const std::string &interface_name)
        {
            auto route = default_network_route();
            if (!proxy_url.empty()) route.proxy_url = proxy_url;
            if (!interface_name.empty()) route.interface_name = interface_name;
            return route;
        }

        void apply_network_route(CURL *curl, const NetworkRoute &route)
        {
            const bool proxied = !route.proxy_url.empty();
            curl_easy_setopt(curl, CURLOPT_PROXY, proxied ? route.proxy_url.c_str() : nullptr);
            // A scheme in the proxy URL (socks5h://, https://, ...) overrides
            // this type; scheme-less proxies are HTTP proxies.
            curl_easy_setopt(curl, CURLOPT_PROXYTYPE, static_cast<long>(CURLPROXY_HTTP));
            // Tunnel through HTTP proxies with CONNECT so TLS stays end to end.
            curl_easy_setopt(curl, CURLOPT_HTTPPROXYTUNNEL, proxied ? 1L : 0L);
            curl_easy_setopt(curl, CURLOPT_PROXY_SSL_VERIFYPEER, 1L);
            curl_easy_setopt(curl, CURLOPT_PROXY_SSL_VERIFYHOST, 2L);
            curl_easy_setopt(curl, CURLOPT_INTERFACE,
                             route.interface_name.empty() ? nullptr : route.interface_name.c_str());
        }
    } // namespace detail
} // namespace polymarket
