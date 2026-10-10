#pragma once

#include "polymarket/http_client.hpp"
#include "polymarket/sdk_error.hpp"

#include <string>

namespace polymarket
{
    // Order-placement eligibility Polymarket reports for the caller's public IP.
    // See https://docs.polymarket.com/api-reference/geoblock
    struct GeoblockStatus
    {
        bool blocked{true};
        std::string ip;
        std::string country; // ISO 3166-1 alpha-2
        std::string region;
    };

    inline constexpr const char *geoblock_base_url = "https://polymarket.com";

    // Queries GET /api/geoblock with the given transport options, so the
    // answer reflects the route (proxy or interface) those options select.
    // Orders from blocked regions are rejected; check before trading and
    // surface the result to users instead of retrying rejected orders.
    Result<GeoblockStatus> check_geoblock(const HttpClientOptions &options = {},
                                          const std::string &base_url = geoblock_base_url);

    // Same query on an existing client whose base URL is the geoblock host, so
    // repeated checks reuse its connection and route.
    Result<GeoblockStatus> check_geoblock(HttpClient &http);
} // namespace polymarket
