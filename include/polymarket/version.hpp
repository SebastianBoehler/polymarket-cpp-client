#pragma once

#define POLYMARKET_CLIENT_VERSION_MAJOR 1
#define POLYMARKET_CLIENT_VERSION_MINOR 1
#define POLYMARKET_CLIENT_VERSION_PATCH 0
#define POLYMARKET_CLIENT_VERSION "1.1.0"

namespace polymarket
{

    inline constexpr int version_major = POLYMARKET_CLIENT_VERSION_MAJOR;
    inline constexpr int version_minor = POLYMARKET_CLIENT_VERSION_MINOR;
    inline constexpr int version_patch = POLYMARKET_CLIENT_VERSION_PATCH;
    inline constexpr const char *version_string = POLYMARKET_CLIENT_VERSION;

} // namespace polymarket
