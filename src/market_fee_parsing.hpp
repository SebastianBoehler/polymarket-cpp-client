#pragma once

#include "rest_numeric.hpp"

#include <nlohmann/json.hpp>

#include <charconv>
#include <stdexcept>
#include <string>

namespace polymarket::detail
{
    struct MarketFeeFields
    {
        bool enabled{false};
        double maker_base_fee{0.0};
        double taker_base_fee{0.0};
        double curve_rate{0.0};
        int curve_exponent{1};
    };

    inline int strict_fee_exponent(const nlohmann::json &value)
    {
        if (value.is_number_integer())
        {
            const auto exponent = value.get<int>();
            if (exponent > 0) return exponent;
        }
        else if (value.is_string())
        {
            const auto &text = value.get_ref<const std::string &>();
            int exponent = 0;
            const auto parsed = std::from_chars(
                text.data(), text.data() + text.size(), exponent);
            if (parsed.ec == std::errc{} &&
                parsed.ptr == text.data() + text.size() && exponent > 0)
                return exponent;
        }
        throw std::invalid_argument("fee exponent must be a positive integer");
    }

    inline MarketFeeFields parse_market_fee_fields(
        const nlohmann::json &item)
    {
        MarketFeeFields fields;
        for (const char *flag : {"fees_enabled", "feesEnabled"})
        {
            if (!item.contains(flag)) continue;
            if (!item[flag].is_boolean())
                throw std::invalid_argument("fees_enabled must be boolean");
            fields.enabled = item[flag].get<bool>();
            break;
        }
        const auto parse_base = [&](const char *snake, const char *camel)
        {
            const char *key = item.contains(snake) ? snake : camel;
            if (!item.contains(key)) return 0.0;
            const double value = strict_json_number(item[key]);
            if (value < 0.0)
                throw std::invalid_argument("base fee must be nonnegative");
            return value;
        };
        fields.maker_base_fee = parse_base("maker_base_fee", "makerBaseFee");
        fields.taker_base_fee = parse_base("taker_base_fee", "takerBaseFee");

        const char *schedule = item.contains("fee_schedule")
                                   ? "fee_schedule"
                                   : "feeSchedule";
        if (item.contains(schedule))
        {
            const auto &fee = item[schedule];
            if (!fee.is_object())
                throw std::invalid_argument("fee schedule must be an object");
            const char *rate = fee.contains("rate") ? "rate" : "r";
            if (fee.contains(rate))
            {
                fields.curve_rate = strict_json_number(fee[rate]);
                if (fields.curve_rate < 0.0 || fields.curve_rate > 1.0)
                    throw std::invalid_argument("fee curve rate must be in [0, 1]");
            }
            const char *exponent = fee.contains("exponent") ? "exponent" : "e";
            if (fee.contains(exponent))
                fields.curve_exponent = strict_fee_exponent(fee[exponent]);
        }
        fields.enabled = fields.enabled || fields.maker_base_fee > 0.0 ||
                         fields.taker_base_fee > 0.0 || fields.curve_rate > 0.0;
        return fields;
    }
}
