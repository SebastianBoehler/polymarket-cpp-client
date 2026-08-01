#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace polymarket::detail
{
    uint64_t parse_iso8601_ms(const std::string &value);
    std::vector<std::string> generate_new_york_hour_slugs(
        const std::string &asset_name, uint64_t now, int count);
}
