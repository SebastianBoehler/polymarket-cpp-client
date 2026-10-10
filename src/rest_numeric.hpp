#pragma once

#include <nlohmann/json.hpp>

#include <cerrno>
#include <clocale>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <stdexcept>
#include <string>

namespace polymarket::detail
{
    using json = nlohmann::json;

    // Optional sign, digits with an optional fraction (or a fraction alone),
    // and an optional exponent.
    inline bool is_decimal_number(const std::string &text)
    {
        std::size_t index = 0;
        const auto digits = [&]
        {
            const auto start = index;
            while (index < text.size() && text[index] >= '0' && text[index] <= '9')
                ++index;
            return index - start;
        };
        const auto sign = [&]
        {
            if (index < text.size() && (text[index] == '+' || text[index] == '-')) ++index;
        };
        sign();
        const auto integer_digits = digits();
        std::size_t fraction_digits = 0;
        if (index < text.size() && text[index] == '.')
        {
            ++index;
            fraction_digits = digits();
        }
        if (integer_digits == 0 && fraction_digits == 0) return false;
        if (index < text.size() && (text[index] == 'e' || text[index] == 'E'))
        {
            ++index;
            sign();
            if (digits() == 0) return false;
        }
        return index == text.size();
    }

    inline double decimal_string_to_double(const std::string &text)
    {
        if (!is_decimal_number(text))
            throw std::invalid_argument("number must be a complete decimal representation");
        // strtod reads the C locale's decimal point.
        const char decimal_point = *std::localeconv()->decimal_point;
        const auto dot = text.find('.');
        std::string localized;
        const char *input = text.c_str();
        if (decimal_point != '.' && dot != std::string::npos)
        {
            localized = text;
            localized[dot] = decimal_point;
            input = localized.c_str();
        }
        char *end = nullptr;
        errno = 0;
        const double number = std::strtod(input, &end);
        if (errno == ERANGE || end != input + text.size())
            throw std::invalid_argument("number must be a complete decimal representation");
        return number;
    }

    inline double strict_json_number(const json &value)
    {
        double number = 0.0;
        if (value.is_string())
        {
            number = decimal_string_to_double(value.get_ref<const std::string &>());
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
