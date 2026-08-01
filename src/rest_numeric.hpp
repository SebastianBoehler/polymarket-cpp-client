#pragma once

#include <nlohmann/json.hpp>

#include <cmath>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>

namespace polymarket::detail
{
    using json = nlohmann::json;

    inline double strict_json_number(const json &value)
    {
        double number = 0.0;
        if (value.is_string())
        {
            const auto &text = value.get_ref<const std::string &>();
            std::istringstream stream(text);
            stream.imbue(std::locale::classic());
            stream >> std::noskipws >> number;
            if (!stream || stream.peek() != std::char_traits<char>::eof())
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
