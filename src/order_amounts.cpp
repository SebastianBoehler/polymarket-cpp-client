#include "order_execution.hpp"

#include "decimal_math.hpp"

#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace polymarket::detail
{
    namespace
    {
        constexpr int token_decimals = 6;

        std::uint64_t power_of_ten(int decimals)
        {
            if (decimals < 0 || decimals > 18)
                throw std::invalid_argument("decimal scale is out of range");
            std::uint64_t scale = 1;
            for (int index = 0; index < decimals; ++index)
                scale *= 10;
            return scale;
        }

        std::uint64_t checked_multiply(std::uint64_t left,
                                       std::uint64_t right)
        {
            if (right != 0 &&
                left > std::numeric_limits<std::uint64_t>::max() / right)
            {
                throw std::out_of_range("order amount exceeds uint64 range");
            }
            return left * right;
        }

        std::uint64_t exact_multiply_divide(std::uint64_t value,
                                            std::uint64_t numerator,
                                            std::uint64_t denominator)
        {
            const auto value_factor = std::gcd(value, denominator);
            value /= value_factor;
            denominator /= value_factor;
            const auto numerator_factor = std::gcd(numerator, denominator);
            numerator /= numerator_factor;
            denominator /= numerator_factor;
            if (denominator != 1)
            {
                throw std::logic_error(
                    "order amount cannot represent the price exactly");
            }
            return checked_multiply(value, numerator);
        }

        std::uint64_t amount_to_micro_units(double amount, int decimals)
        {
            if (!std::isfinite(amount) || amount <= 0.0 ||
                decimals > token_decimals)
            {
                throw std::invalid_argument(
                    "order amount must be finite and positive");
            }
            const auto units = decimal_to_scaled_uint64(
                amount, decimals, DecimalRoundingMode::Down);
            const auto micro_units = checked_multiply(
                units, power_of_ten(token_decimals - decimals));
            if (micro_units == 0)
                throw std::invalid_argument("order amount rounds to zero");
            return micro_units;
        }

        ValidatedOrderPrice quantized_price(
            double price, const OrderRoundingConfig &rounding,
            DecimalRoundingMode mode)
        {
            if (!std::isfinite(price) || price <= 0.0)
                throw std::invalid_argument("order price must be finite and positive");
            const auto scale = power_of_ten(rounding.price_decimals);
            const auto units = decimal_to_scaled_uint64(
                price, rounding.price_decimals, mode);
            if (units == 0 || units >= scale)
                throw std::invalid_argument("order price is outside the market range");
            return {units, scale, rounding};
        }

        OrderAmounts amounts_for_shares(OrderSide side, double size,
                                        const ValidatedOrderPrice &price)
        {
            const auto shares = amount_to_micro_units(
                size, price.rounding.size_decimals);
            const auto cost = exact_multiply_divide(
                shares, price.units, price.scale);
            return side == OrderSide::BUY ? OrderAmounts{cost, shares}
                                          : OrderAmounts{shares, cost};
        }
    }

    OrderRoundingConfig rounding_config_for_tick_size(
        const std::string &tick_size)
    {
        if (tick_size == "0.1") return {1, 2, 3, 1};
        if (tick_size == "0.01") return {2, 2, 4, 1};
        if (tick_size == "0.005") return {3, 2, 5, 5};
        if (tick_size == "0.0025") return {4, 2, 6, 25};
        if (tick_size == "0.001") return {3, 2, 5, 1};
        if (tick_size == "0.0001") return {4, 2, 6, 1};
        throw std::invalid_argument("unsupported tick size: " + tick_size);
    }

    ValidatedOrderPrice validate_order_price(double price,
                                             const std::string &tick_size)
    {
        const auto rounding = rounding_config_for_tick_size(tick_size);
        const auto scale = power_of_ten(rounding.price_decimals);
        const auto units = exact_decimal_to_scaled_uint64(
            price, rounding.price_decimals);
        if (units < rounding.tick_units ||
            units > scale - rounding.tick_units)
        {
            throw std::invalid_argument(
                "order price is outside the market tick range");
        }
        if (units % rounding.tick_units != 0)
        {
            throw std::invalid_argument(
                "order price is not an exact tick multiple");
        }
        return {units, scale, rounding};
    }

    OrderAmounts calculate_limit_order_amounts(OrderSide side, double price,
                                               double size)
    {
        return calculate_limit_order_amounts(
            side, price, size, rounding_config_for_tick_size("0.01"));
    }

    OrderAmounts calculate_limit_order_amounts(
        OrderSide side, double price, double size,
        const OrderRoundingConfig &rounding)
    {
        return amounts_for_shares(
            side, size,
            quantized_price(price, rounding, DecimalRoundingMode::HalfEven));
    }

    OrderAmounts calculate_limit_order_amounts(
        OrderSide side, double size, const ValidatedOrderPrice &price)
    {
        return amounts_for_shares(side, size, price);
    }

    OrderAmounts calculate_market_order_amounts(
        OrderSide side, double amount, double price,
        const OrderRoundingConfig &rounding)
    {
        return calculate_market_order_amounts(
            side, amount,
            quantized_price(price, rounding, DecimalRoundingMode::Down));
    }

    OrderAmounts calculate_market_order_amounts(
        OrderSide side, double amount, const ValidatedOrderPrice &price)
    {
        if (side == OrderSide::SELL)
            return amounts_for_shares(side, amount, price);

        const auto budget = amount_to_micro_units(
            amount, price.rounding.size_decimals);
        const auto maker_step = price.units /
                                std::gcd(price.units, price.scale);
        const auto maker = budget - budget % maker_step;
        if (maker == 0)
        {
            throw std::invalid_argument(
                "market BUY budget is below one exact price unit");
        }
        const auto taker = exact_multiply_divide(
            maker, price.scale, price.units);
        return {maker, taker};
    }
}
