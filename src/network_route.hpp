#pragma once

#include "polymarket/network.hpp"

#include <curl/curl.h>

#include <string>

namespace polymarket::detail
{
    // Fills each empty field from the process-wide default route.
    NetworkRoute resolve_network_route(const std::string &proxy_url,
                                       const std::string &interface_name);

    // Applies a route to an easy handle. The handle copies both strings.
    // An empty proxy restores libcurl's proxy environment handling.
    void apply_network_route(CURL *curl, const NetworkRoute &route);
} // namespace polymarket::detail
