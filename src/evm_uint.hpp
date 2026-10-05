#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace polymarket::detail
{
    // Big-endian 32-byte word from a base-10 string or a 0x-prefixed hex string.
    // Throws std::invalid_argument on empty, malformed, signed or > uint256 input.
    std::vector<uint8_t> parse_uint256_word(const std::string &text);

    // Big-endian 32-byte word holding value.
    std::vector<uint8_t> uint256_word(uint64_t value);
}
