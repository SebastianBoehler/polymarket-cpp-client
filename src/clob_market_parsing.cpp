#include "market_fetcher.hpp"
#include "market_fee_parsing.hpp"
#include "market_time.hpp"
#include "rest_numeric.hpp"

#include <nlohmann/json.hpp>

#include <stdexcept>

namespace polymarket::detail
{
    namespace
    {
        using json = nlohmann::json;

        std::string optional_string(const json &item, const char *field)
        {
            if (!item.contains(field)) return {};
            if (!item[field].is_string())
                throw std::invalid_argument(
                    std::string("market ") + field + " must be a string");
            return item[field].get<std::string>();
        }

        std::string required_string(const json &item, const char *field)
        {
            const auto value = optional_string(item, field);
            if (value.empty())
                throw std::invalid_argument(
                    std::string("market ") + field + " must not be empty");
            return value;
        }

        bool optional_bool(const json &item, const char *field)
        {
            if (!item.contains(field)) return false;
            if (!item[field].is_boolean())
                throw std::invalid_argument(
                    std::string("market ") + field + " must be boolean");
            return item[field].get<bool>();
        }

        std::string decimal_string(const json &value)
        {
            return value.is_string() ? value.get<std::string>() : value.dump();
        }

        std::vector<Token> parse_tokens(const json &item)
        {
            if (!item.contains("tokens") || !item["tokens"].is_array() ||
                item["tokens"].empty())
                throw std::invalid_argument("market tokens must be a nonempty array");
            std::vector<Token> tokens;
            tokens.reserve(item["tokens"].size());
            for (const auto &value : item["tokens"])
            {
                if (!value.is_object())
                    throw std::invalid_argument("market token must be an object");
                tokens.push_back({required_string(value, "token_id"),
                                  required_string(value, "outcome")});
            }
            return tokens;
        }

        ClobMarket parse_market(const json &item)
        {
            if (!item.is_object())
                throw std::invalid_argument("market must be an object");
            ClobMarket market;
            market.condition_id = required_string(item, "condition_id");
            market.question = optional_string(item, "question");
            market.market_slug = optional_string(item, "market_slug");
            market.neg_risk = optional_bool(item, "neg_risk");
            market.active = optional_bool(item, "active");
            market.closed = optional_bool(item, "closed");
            if (item.contains("minimum_order_size"))
            {
                market.minimum_order_size =
                    strict_json_number(item["minimum_order_size"]);
                if (market.minimum_order_size <= 0.0)
                    throw std::invalid_argument(
                        "minimum_order_size must be positive");
            }
            if (item.contains("minimum_tick_size"))
            {
                (void)json_orderbook_price(item["minimum_tick_size"]);
                market.minimum_tick_size =
                    decimal_string(item["minimum_tick_size"]);
            }
            const auto end_date = optional_string(item, "end_date_iso");
            market.end_time_ms = parse_iso8601_ms(end_date);
            const auto fees = parse_market_fee_fields(item);
            market.fees_enabled = fees.enabled;
            market.maker_base_fee = fees.maker_base_fee;
            market.taker_base_fee = fees.taker_base_fee;
            market.fee_rate = fees.curve_rate;
            market.fee_exponent = fees.curve_exponent;
            market.tokens = parse_tokens(item);
            return market;
        }

        std::vector<ClobMarket> parse_items(const json &items)
        {
            if (!items.is_array())
                throw std::invalid_argument("market data must be an array");
            std::vector<ClobMarket> markets;
            markets.reserve(items.size());
            for (const auto &item : items)
            {
                try
                {
                    markets.push_back(parse_market(item));
                }
                catch (const std::exception &)
                {
                    // Historical market pages can contain retired entries with
                    // no tokens. Omit invalid records instead of materializing
                    // a default market or invalidating unrelated valid rows.
                }
            }
            return markets;
        }
    }

    std::vector<ClobMarket> parse_clob_markets_json(
        const std::string &json_text)
    {
        const auto parsed = json::parse(json_text);
        if (parsed.is_array()) return parse_items(parsed);
        if (!parsed.is_object() || !parsed.contains("data"))
            throw std::invalid_argument(
                "market response must be an array or page object");
        return parse_items(parsed["data"]);
    }

    ClobMarketPage parse_clob_market_page_json(const std::string &json_text)
    {
        const auto parsed = json::parse(json_text);
        if (!parsed.is_object() || !parsed.contains("data") ||
            !parsed.contains("next_cursor") ||
            !parsed["next_cursor"].is_string())
            throw std::invalid_argument(
                "market page requires data and next_cursor");
        return {parse_items(parsed["data"]),
                parsed["next_cursor"].get<std::string>()};
    }
}
