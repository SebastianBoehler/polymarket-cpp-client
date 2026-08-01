#pragma once

#include "clob_client.hpp"
#include "query_encoding.hpp"
#include "rest_numeric.hpp"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <map>
#include <optional>
#include <set>

namespace polymarket::detail
{
    using json = nlohmann::json;

    struct ParsedOpenOrderPage
    {
        std::vector<OpenOrder> orders;
        std::string next_cursor;
    };

    struct ParsedCancellationResponse
    {
        std::set<std::string> canceled;
        std::map<std::string, std::string> not_canceled;
    };

    struct ParsedTradePage
    {
        std::vector<Trade> trades;
        std::string next_cursor;
    };

    OpenOrder parse_open_order_json(const std::string &json_text);
    std::vector<OpenOrder> parse_open_orders_json(const std::string &json_text);
    ParsedOpenOrderPage parse_open_order_page_json(const std::string &json_text);
    OrderResponse parse_order_response_json_strict(const std::string &json_text);
    ParsedCancellationResponse parse_cancellation_response_json(
        const std::string &json_text);
    ParsedTradePage parse_trade_page_json(const std::string &json_text);

    inline std::string uppercase(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c)
                       { return static_cast<char>(std::toupper(c)); });
        return value;
    }

    inline json book_request_body(const std::vector<std::string> &token_ids,
                                  const std::optional<std::string> &side = std::nullopt)
    {
        auto body = json::array();
        for (const auto &token_id : token_ids)
        {
            json request{{"token_id", token_id}};
            if (side)
            {
                request["side"] = uppercase(*side);
            }
            body.push_back(std::move(request));
        }
        return body;
    }

    inline std::string json_scalar_string(const json &value, const std::string &fallback = "")
    {
        if (value.is_null())
            return fallback;
        return value.is_string() ? value.get<std::string>() : value.dump();
    }

    inline constexpr const char *INITIAL_CURSOR = "MA==";
    inline constexpr const char *END_CURSOR = "LTE=";

    ClobClient::RewardsInfo parse_current_reward_info(const json &item);
    ClobClient::RewardsInfo parse_market_reward_info(const json &item);
    ClobClient::EarningsInfo parse_user_earning_info(const json &item);
    ClobClient::EarningsInfo parse_total_user_earning_info(const json &item);
}
