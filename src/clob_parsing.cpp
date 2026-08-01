#include "clob_client.hpp"
#include "clob_client_internal.hpp"
#include "market_fetcher.hpp"
#include "rest_orderbook_parsing.hpp"

#include <nlohmann/json.hpp>
#include <stdexcept>

using json = nlohmann::json;

namespace polymarket
{
    using namespace detail;

    namespace
    {
        std::string required_order_string(const json &item, const char *field)
        {
            const auto &value = item.at(field);
            if (!value.is_string())
            {
                throw std::invalid_argument(std::string("open order ") + field +
                                            " must be a string");
            }
            return value.get<std::string>();
        }

        std::string required_order_timestamp(const json &item)
        {
            const auto &value = item.at("created_at");
            if (value.is_string())
            {
                return value.get<std::string>();
            }
            if (value.is_number_integer() || value.is_number_unsigned())
            {
                return value.dump();
            }
            throw std::invalid_argument(
                "open order created_at must be an integer or string");
        }

        std::vector<std::string> optional_order_string_array(
            const json &item, const char *field)
        {
            if (!item.contains(field) || item.at(field).is_null()) return {};
            const auto &values = item.at(field);
            if (!values.is_array())
                throw std::invalid_argument(std::string("open order ") + field +
                                            " must be an array");

            std::vector<std::string> result;
            result.reserve(values.size());
            for (const auto &value : values)
            {
                if (!value.is_string())
                    throw std::invalid_argument(std::string("open order ") + field +
                                                " entries must be strings");
                result.push_back(value.get<std::string>());
            }
            return result;
        }

        OpenOrder parse_open_order_object(const json &item)
        {
            if (!item.is_object())
            {
                throw std::invalid_argument("open order item must be an object");
            }

            OpenOrder order;
            order.id = required_order_string(item, "id");
            order.owner = required_order_string(item, "owner");
            order.maker_address = required_order_string(item, "maker_address");
            order.market = required_order_string(item, "market");
            order.asset_id = required_order_string(item, "asset_id");
            order.side = required_order_string(item, "side");
            order.original_size = required_order_string(item, "original_size");
            order.size_matched = required_order_string(item, "size_matched");
            order.price = required_order_string(item, "price");
            order.status = required_order_string(item, "status");
            order.associate_trades = optional_order_string_array(
                item, "associate_trades");
            order.outcome = required_order_string(item, "outcome");
            order.created_at = required_order_timestamp(item);
            order.expiration = required_order_string(item, "expiration");
            order.order_type = required_order_string(item, "order_type");
            return order;
        }

        std::vector<OpenOrder> parse_open_order_array(const json &items)
        {
            if (!items.is_array())
            {
                throw std::invalid_argument("open orders data must be an array");
            }
            std::vector<OpenOrder> orders;
            orders.reserve(items.size());
            for (const auto &item : items)
            {
                orders.push_back(parse_open_order_object(item));
            }
            return orders;
        }
    }

    OpenOrder detail::parse_open_order_json(const std::string &json_text)
    {
        return parse_open_order_object(json::parse(json_text));
    }

    std::vector<OpenOrder> detail::parse_open_orders_json(
        const std::string &json_text)
    {
        const auto parsed = json::parse(json_text);
        if (parsed.is_array())
        {
            return parse_open_order_array(parsed);
        }
        if (!parsed.is_object() || !parsed.contains("data"))
        {
            throw std::invalid_argument(
                "open orders response must be an array or paginated object");
        }
        return parse_open_order_array(parsed.at("data"));
    }

    detail::ParsedOpenOrderPage detail::parse_open_order_page_json(
        const std::string &json_text)
    {
        const auto parsed = json::parse(json_text);
        if (!parsed.is_object())
        {
            throw std::invalid_argument("open orders response must be an object");
        }
        const auto &cursor = parsed.at("next_cursor");
        if (!cursor.is_string())
        {
            throw std::invalid_argument("open orders next_cursor must be a string");
        }
        return {parse_open_order_array(parsed.at("data")),
                cursor.get<std::string>()};
    }

    std::vector<ClobMarket> ClobClient::parse_markets(const std::string &json_str)
    {
        try
        {
            return detail::parse_clob_markets_json(json_str);
        }
        catch (...)
        {
            return {};
        }
    }

    std::optional<Orderbook> ClobClient::parse_orderbook(
        const std::string &json_str, const std::string &expected_asset_id)
    {
        try
        {
            return detail::parse_rest_orderbook_json(json_str,
                                                     expected_asset_id);
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    OrderResponse ClobClient::parse_order_response(const std::string &json_str)
    {
        OrderResponse result;
        result.success = false;

        try
        {
            auto j = json::parse(json_str);

            result.success = j.value("success", false);
            result.error_msg = j.value("errorMsg", "");
            result.order_id = j.value("orderID", "");
            result.status = j.value("status", "");
            result.taking_amount = j.value("takingAmount", "0");
            result.making_amount = j.value("makingAmount", "0");

            if (j.contains("transactionsHashes") && j["transactionsHashes"].is_array())
            {
                for (const auto &hash : j["transactionsHashes"])
                {
                    result.transaction_hashes.push_back(hash.get<std::string>());
                }
            }
        }
        catch (...)
        {
        }

        return result;
    }

    std::vector<OpenOrder> ClobClient::parse_open_orders(const std::string &json_str)
    {
        try
        {
            return detail::parse_open_orders_json(json_str);
        }
        catch (...)
        {
            return {};
        }
    }

}
