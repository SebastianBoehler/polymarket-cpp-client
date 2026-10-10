#pragma once

#include "rest_numeric.hpp"
#include "polymarket/types.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

namespace polymarket::detail
{
    inline void append_rest_levels(std::vector<PriceLevel> &levels,
                                   const json &values)
    {
        if (!values.is_array())
            throw std::invalid_argument("orderbook levels must be an array");
        const auto first = levels.size();
        levels.reserve(first + values.size());
        for (const auto &value : values)
        {
            if (!value.is_object())
                throw std::invalid_argument("orderbook level must be an object");
            levels.push_back({json_orderbook_price(value.at("price")),
                              json_nonnegative_number(value.at("size"))});
        }
        std::vector<double> prices;
        prices.reserve(levels.size() - first);
        for (auto level = levels.begin() + static_cast<std::ptrdiff_t>(first);
             level != levels.end(); ++level)
            prices.push_back(level->price);
        std::sort(prices.begin(), prices.end());
        if (std::adjacent_find(prices.begin(), prices.end()) != prices.end())
            throw std::invalid_argument("orderbook contains a duplicate price level");
    }

    inline Orderbook parse_rest_orderbook(const json &parsed,
                                          const std::string &expected_asset_id = {})
    {
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

    inline Orderbook parse_rest_orderbook_json(const std::string &json_text,
                                               const std::string &expected_asset_id = {})
    {
        return parse_rest_orderbook(json::parse(json_text), expected_asset_id);
    }
}
