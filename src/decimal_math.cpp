#include "decimal_math.hpp"

#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace polymarket
{
    namespace
    {
        long double decimal_scale(int decimals)
        {
            if (decimals < 0 || decimals > 18)
            {
                throw std::invalid_argument("decimals must be between 0 and 18");
            }

            long double scale = 1.0L;
            for (int i = 0; i < decimals; ++i)
            {
                scale *= 10.0L;
            }
            return scale;
        }
    }

    std::string decimal_to_scaled_integer(double amount,
                                          int decimals,
                                          DecimalRoundingMode rounding_mode)
    {
        if (!std::isfinite(amount) || amount < 0.0)
        {
            throw std::invalid_argument("amount must be finite and non-negative");
        }

        const long double scaled = static_cast<long double>(amount) * decimal_scale(decimals);
        const long double integer_value = rounding_mode == DecimalRoundingMode::Nearest
                                              ? std::floor(scaled + 0.5L)
                                              : std::floor(scaled + 1e-9L);

        if (integer_value > static_cast<long double>(std::numeric_limits<std::uint64_t>::max()))
        {
            throw std::out_of_range("scaled amount exceeds uint64 range");
        }

        return std::to_string(static_cast<std::uint64_t>(integer_value));
    }
}
