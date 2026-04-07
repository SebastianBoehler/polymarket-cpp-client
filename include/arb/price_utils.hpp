#pragma once

#include <algorithm>
#include <cmath>

namespace polymarket::arb
{

    inline double clamp_price(double price)
    {
        return std::clamp(price, 0.0, 0.99);
    }

    inline double round_up_to_tick(double value, double tick_size)
    {
        if (tick_size <= 0.0)
        {
            return value;
        }
        return std::ceil(value / tick_size) * tick_size;
    }

    inline double round_down_to_tick(double value, double tick_size)
    {
        if (tick_size <= 0.0)
        {
            return value;
        }
        return std::floor(value / tick_size) * tick_size;
    }

    inline double round_shares_for_polymarket(double shares)
    {
        double rounded_up = std::ceil(shares * 100000000.0) / 100000000.0;
        return std::floor(rounded_up * 10000.0) / 10000.0;
    }

    inline double round_down_currency(double amount)
    {
        return std::floor(amount * 1000000.0) / 1000000.0;
    }

    inline double best_ask_notional(double best_ask, double size)
    {
        return best_ask * size;
    }

} // namespace polymarket::arb
