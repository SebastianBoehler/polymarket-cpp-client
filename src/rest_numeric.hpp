#pragma once

#include <nlohmann/json.hpp>

#include <charconv>
#include <cmath>
#include <stdexcept>
#include <string>
#include <system_error>

namespace polymarket::detail
{
    using json = nlohmann::json;

    inline double strict_json_number(const json &value)
    {
        double number = 0.0;
        if (value.is_string())
        {
            const auto &text = value.get_ref<const std::string &>();
            const char *begin = text.data();
            const char *const end = begin + text.size();
            // Accepts one optional leading '+' sign.
            if (end - begin > 1 && *begin == '+' && begin[1] != '+' && begin[1] != '-') ++begin;
            const auto [stop, error] = std::from_chars(begin, end, number);
            if (error != std::errc() || stop != end)
                throw std::invalid_argument("number must be a complete decimal representation");
        }
        else if (value.is_number())
        {
            number = value.get<double>();
        }
        else
        {
            throw std::invalid_argument("number must be a JSON number or numeric string");
        }
        if (!std::isfinite(number))
            throw std::invalid_argument("number must be finite");
        return number;
    }

    inline double json_probability(const json &value)
    {
        const double number = strict_json_number(value);
        if (number < 0.0 || number > 1.0)
            throw std::out_of_range("probability must be in [0, 1]");
        return number;
    }

    inline double json_orderbook_price(const json &value)
    {
        const double number = strict_json_number(value);
        if (number <= 0.0 || number >= 1.0)
            throw std::out_of_range("orderbook price must be in (0, 1)");
        return number;
    }

    inline double json_nonnegative_number(const json &value)
    {
        const double number = strict_json_number(value);
        if (number < 0.0)
            throw std::out_of_range("number must be nonnegative");
        return number;
    }
}
