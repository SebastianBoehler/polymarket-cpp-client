#pragma once

#include "rest_numeric.hpp"
#include "types.hpp"

#include <nlohmann/json.hpp>

#include <stdexcept>
#include <string>
#include <unordered_set>

namespace polymarket::detail
{
    inline void append_rest_levels(std::vector<PriceLevel> &levels,
                                   const json &values)
    {
        if (!values.is_array())
            throw std::invalid_argument("orderbook levels must be an array");
        levels.reserve(values.size());
        std::unordered_set<double> prices;
        for (const auto &value : values)
        {
            if (!value.is_object())
                throw std::invalid_argument("orderbook level must be an object");
            PriceLevel level{json_orderbook_price(value.at("price")),
                             json_nonnegative_number(value.at("size"))};
            if (!prices.insert(level.price).second)
                throw std::invalid_argument(
                    "orderbook contains a duplicate price level");
            levels.push_back(level);
        }
    }

    inline Orderbook parse_rest_orderbook_json(
        const std::string &json_text,
        const std::string &expected_asset_id = {})
    {
        const auto parsed = json::parse(json_text);
        if (!parsed.is_object())
            throw std::invalid_argument("orderbook must be an object");
        if (!parsed.contains("asset_id") || !parsed["asset_id"].is_string())
            throw std::invalid_argument(
                "orderbook requires a string asset_id");
        if (!parsed.contains("bids") || !parsed.contains("asks"))
            throw std::invalid_argument(
                "orderbook requires bids and asks arrays");

        Orderbook book;
        book.timestamp_ns = now_ns();
        book.asset_id = parsed["asset_id"].get<std::string>();
        if (book.asset_id.empty() ||
            (!expected_asset_id.empty() && book.asset_id != expected_asset_id))
            throw std::invalid_argument("orderbook asset_id does not match request");
        append_rest_levels(book.bids, parsed["bids"]);
        append_rest_levels(book.asks, parsed["asks"]);
        return book;
    }
}
