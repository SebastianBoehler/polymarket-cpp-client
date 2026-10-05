#include "evm_rlp.hpp"

namespace polymarket::detail
{
    namespace
    {
        std::vector<uint8_t> minimal_length_bytes(size_t length)
        {
            std::vector<uint8_t> bytes;
            for (; length != 0; length >>= 8)
                bytes.insert(bytes.begin(), static_cast<uint8_t>(length & 0xff));
            return bytes;
        }

        // Short payloads (< 56 bytes) put the length in the prefix byte; longer
        // ones put the byte count of the big-endian length there instead.
        std::vector<uint8_t> with_header(uint8_t short_base, uint8_t long_base,
                                         const std::vector<uint8_t> &payload)
        {
            std::vector<uint8_t> out;
            if (payload.size() < 56)
            {
                out.push_back(static_cast<uint8_t>(short_base + payload.size()));
            }
            else
            {
                const auto length = minimal_length_bytes(payload.size());
                out.push_back(static_cast<uint8_t>(long_base + length.size()));
                out.insert(out.end(), length.begin(), length.end());
            }
            out.insert(out.end(), payload.begin(), payload.end());
            return out;
        }
    } // namespace

    std::vector<uint8_t> rlp_encode_bytes(const std::vector<uint8_t> &bytes)
    {
        if (bytes.size() == 1 && bytes[0] < 0x80)
            return {bytes[0]};
        return with_header(0x80, 0xb7, bytes);
    }

    std::vector<uint8_t> rlp_encode_uint(const std::vector<uint8_t> &big_endian)
    {
        size_t first = 0;
        while (first < big_endian.size() && big_endian[first] == 0)
            ++first;
        return rlp_encode_bytes(std::vector<uint8_t>(big_endian.begin() + static_cast<std::ptrdiff_t>(first),
                                                     big_endian.end()));
    }

    std::vector<uint8_t> rlp_encode_list(const std::vector<std::vector<uint8_t>> &encoded_items)
    {
        std::vector<uint8_t> payload;
        for (const auto &item : encoded_items)
            payload.insert(payload.end(), item.begin(), item.end());
        return with_header(0xc0, 0xf7, payload);
    }
}
