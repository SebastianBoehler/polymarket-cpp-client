#pragma once

#include <cstdint>
#include <string>

namespace polymarket
{
    enum class DecimalRoundingMode
    {
        Down,
        Nearest,
        HalfEven
    };

    std::uint64_t decimal_to_scaled_uint64(
        double amount,
        int decimals,
        DecimalRoundingMode rounding_mode = DecimalRoundingMode::Down);
    std::uint64_t exact_decimal_to_scaled_uint64(double amount, int decimals);

    std::string decimal_to_scaled_integer(double amount,
                                          int decimals,
                                          DecimalRoundingMode rounding_mode = DecimalRoundingMode::Down);
}
