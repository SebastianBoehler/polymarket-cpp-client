#pragma once

#include <string>
#include <string_view>

namespace polymarket::detail
{
    inline std::string percent_encode_query_value(std::string_view value)
    {
        constexpr char HEX[] = "0123456789ABCDEF";
        std::string encoded;
        encoded.reserve(value.size());
        for (const unsigned char character : value)
        {
            const bool unreserved = (character >= 'A' && character <= 'Z') ||
                                    (character >= 'a' && character <= 'z') ||
                                    (character >= '0' && character <= '9') ||
                                    character == '-' || character == '.' ||
                                    character == '_' || character == '~';
            if (unreserved)
            {
                encoded.push_back(static_cast<char>(character));
                continue;
            }
            encoded.push_back('%');
            encoded.push_back(HEX[character >> 4]);
            encoded.push_back(HEX[character & 0x0F]);
        }
        return encoded;
    }
}
