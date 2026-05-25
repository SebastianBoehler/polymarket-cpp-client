#pragma once

#include <string>

namespace polymarket
{
    enum class DecimalRoundingMode
    {
        Down,
        Nearest
    };

    std::string decimal_to_scaled_integer(double amount,
                                          int decimals,
                                          DecimalRoundingMode rounding_mode = DecimalRoundingMode::Down);
}
