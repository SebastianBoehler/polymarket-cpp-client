#pragma once

#include <cstdint>
#include <vector>

namespace polymarket::detail
{
    // RLP string item.
    std::vector<uint8_t> rlp_encode_bytes(const std::vector<uint8_t> &bytes);

    // RLP scalar: big-endian bytes with leading zeros removed (zero is the empty string).
    std::vector<uint8_t> rlp_encode_uint(const std::vector<uint8_t> &big_endian);

    // RLP list of already-encoded items.
    std::vector<uint8_t> rlp_encode_list(const std::vector<std::vector<uint8_t>> &encoded_items);
}
