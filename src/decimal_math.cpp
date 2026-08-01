#include "decimal_math.hpp"

#include <algorithm>
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

        long double round_down_with_ulp_correction(double amount,
                                                   long double scaled,
                                                   long double scale)
        {
            const long double lower = std::floor(scaled);
            if (scaled == lower)
            {
                return lower;
            }
            const double next = std::nextafter(amount,
                                               std::numeric_limits<double>::infinity());
            if (!std::isfinite(next))
            {
                return lower;
            }
            const long double next_scaled = next * static_cast<double>(scale);
            return next_scaled >= lower + 1.0L ? lower + 1.0L : lower;
        }

        long double scaled_input_ulp(double amount, long double scale)
        {
            const double above = std::nextafter(
                amount, std::numeric_limits<double>::infinity());
            const double below = std::nextafter(
                amount, -std::numeric_limits<double>::infinity());
            const long double above_distance = std::isfinite(above)
                                                   ? static_cast<long double>(above - amount)
                                                   : 0.0L;
            const long double below_distance = std::isfinite(below)
                                                   ? static_cast<long double>(amount - below)
                                                   : 0.0L;
            return std::max(above_distance, below_distance) * scale;
        }

        long double round_half_even(double amount, long double scaled,
                                    long double scale)
        {
            const long double lower = std::floor(scaled);
            const long double fraction = scaled - lower;
            const long double tolerance = scaled_input_ulp(amount, scale);
            if (std::abs(fraction - 0.5L) <= tolerance)
            {
                return std::fmod(lower, 2.0L) == 0.0L ? lower : lower + 1.0L;
            }
            return fraction < 0.5L ? lower : lower + 1.0L;
        }

        std::uint64_t checked_uint64(long double integer_value)
        {
            const long double exclusive_upper_bound = std::ldexp(1.0L, 64);
            if (!std::isfinite(integer_value) || integer_value < 0.0L ||
                integer_value >= exclusive_upper_bound)
            {
                throw std::out_of_range("scaled amount exceeds uint64 range");
            }
            return static_cast<std::uint64_t>(integer_value);
        }
    }

    std::uint64_t decimal_to_scaled_uint64(double amount, int decimals,
                                           DecimalRoundingMode rounding_mode)
    {
        if (!std::isfinite(amount) || amount < 0.0)
        {
            throw std::invalid_argument("amount must be finite and non-negative");
        }

        const long double scale = decimal_scale(decimals);
        const long double scaled = amount * static_cast<double>(scale);
        long double integer_value = 0.0L;
        if (rounding_mode == DecimalRoundingMode::Nearest)
            integer_value = std::floor(scaled + 0.5L);
        else if (rounding_mode == DecimalRoundingMode::HalfEven)
            integer_value = round_half_even(amount, scaled, scale);
        else
            integer_value = round_down_with_ulp_correction(amount, scaled, scale);
        return checked_uint64(integer_value);
    }

    std::uint64_t exact_decimal_to_scaled_uint64(double amount, int decimals)
    {
        if (!std::isfinite(amount) || amount < 0.0)
        {
            throw std::invalid_argument("amount must be finite and non-negative");
        }
        const long double scale = decimal_scale(decimals);
        const long double scaled = amount * static_cast<double>(scale);
        const long double nearest = std::floor(scaled + 0.5L);
        if (!std::isfinite(scaled) ||
            std::abs(scaled - nearest) > 2.0L * scaled_input_ulp(amount, scale))
        {
            throw std::invalid_argument("amount has excess decimal precision");
        }
        return checked_uint64(nearest);
    }

    std::string decimal_to_scaled_integer(double amount, int decimals,
                                          DecimalRoundingMode rounding_mode)
    {
        return std::to_string(
            decimal_to_scaled_uint64(amount, decimals, rounding_mode));
    }
}
