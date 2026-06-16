#pragma once

#define POLYMARKET_CLIENT_VERSION_MAJOR 1
#define POLYMARKET_CLIENT_VERSION_MINOR 2
#define POLYMARKET_CLIENT_VERSION_PATCH 5
#define POLYMARKET_CLIENT_VERSION "1.2.5"

namespace polymarket
{

    inline constexpr int version_major = POLYMARKET_CLIENT_VERSION_MAJOR;
    inline constexpr int version_minor = POLYMARKET_CLIENT_VERSION_MINOR;
    inline constexpr int version_patch = POLYMARKET_CLIENT_VERSION_PATCH;
    inline constexpr const char *version_string = POLYMARKET_CLIENT_VERSION;

} // namespace polymarket
