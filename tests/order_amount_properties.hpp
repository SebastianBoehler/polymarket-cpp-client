#pragma once

#include "order_execution.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <numeric>

namespace order_amount_properties
{
    inline bool exact_ratio(const polymarket::detail::OrderAmounts &amounts,
                            polymarket::OrderSide side,
                            const polymarket::detail::ValidatedOrderPrice &price)
    {
        if (side == polymarket::OrderSide::BUY)
            return amounts.maker * price.scale == amounts.taker * price.units;
        return amounts.taker * price.scale == amounts.maker * price.units;
    }

    inline bool throws_validation(double price, const char *tick)
    {
        try
        {
            (void)polymarket::detail::validate_order_price(price, tick);
        }
        catch (const std::exception &)
        {
            return true;
        }
        return false;
    }

    inline bool run()
    {
        using namespace polymarket;
        struct TickCase
        {
            const char *tick;
            double price;
        };
        const std::array<TickCase, 6> cases{{
            {"0.1", 0.5},
            {"0.01", 0.57},
            {"0.005", 0.515},
            {"0.0025", 0.5125},
            {"0.001", 0.513},
            {"0.0001", 0.5125},
        }};

        for (const auto &test : cases)
        {
            const auto price = detail::validate_order_price(test.price, test.tick);
            const auto below = detail::validate_order_price(
                std::nextafter(test.price, 0.0), test.tick);
            const auto above = detail::validate_order_price(
                std::nextafter(test.price, 1.0), test.tick);
            const double boundary_size = std::nextafter(5.29, 0.0);
            const auto limit_buy = detail::calculate_limit_order_amounts(
                OrderSide::BUY, boundary_size, price);
            const auto limit_sell = detail::calculate_limit_order_amounts(
                OrderSide::SELL, boundary_size, price);
            const auto market_buy = detail::calculate_market_order_amounts(
                OrderSide::BUY, boundary_size, price);
            const auto market_sell = detail::calculate_market_order_amounts(
                OrderSide::SELL, boundary_size, price);
            const auto maker_step = price.units / std::gcd(price.units, price.scale);
            const bool valid = below.units == price.units && above.units == price.units &&
                               limit_buy.taker == 5290000 &&
                               limit_sell.maker == 5290000 &&
                               market_sell.maker == 5290000 &&
                               market_buy.maker <= 5290000 &&
                               5290000 - market_buy.maker < maker_step &&
                               exact_ratio(limit_buy, OrderSide::BUY, price) &&
                               exact_ratio(limit_sell, OrderSide::SELL, price) &&
                               exact_ratio(market_buy, OrderSide::BUY, price) &&
                               exact_ratio(market_sell, OrderSide::SELL, price);
            if (!valid)
            {
                std::cerr << "fixed-point property failed for tick " << test.tick << '\n';
                return false;
            }
        }

        if (!throws_validation(0.45, "0.1") ||
            !throws_validation(0.513, "0.005") ||
            !throws_validation(std::numeric_limits<double>::quiet_NaN(), "0.01") ||
            !throws_validation(std::numeric_limits<double>::infinity(), "0.01"))
        {
            std::cerr << "strict public tick-grid validation failed\n";
            return false;
        }
        return true;
    }
}
