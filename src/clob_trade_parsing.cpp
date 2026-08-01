#include "clob_client_internal.hpp"

#include <limits>
#include <stdexcept>

namespace polymarket::detail
{
    namespace
    {
        std::string required_string(const json &item, const char *field,
                                    const char *context)
        {
            const auto &value = item.at(field);
            if (!value.is_string())
            {
                throw std::invalid_argument(std::string(context) + " " + field +
                                            " must be a string");
            }
            return value.get<std::string>();
        }

        uint32_t required_bucket_index(const json &item)
        {
            const auto &value = item.at("bucket_index");
            if (!value.is_number_integer() && !value.is_number_unsigned())
            {
                throw std::invalid_argument(
                    "trade bucket_index must be an unsigned integer");
            }

            uint64_t index = 0;
            if (value.is_number_unsigned())
            {
                index = value.get<uint64_t>();
            }
            else
            {
                const auto signed_index = value.get<int64_t>();
                if (signed_index < 0)
                {
                    throw std::out_of_range(
                        "trade bucket_index is outside uint32 range");
                }
                index = static_cast<uint64_t>(signed_index);
            }
            if (index > std::numeric_limits<uint32_t>::max())
            {
                throw std::out_of_range("trade bucket_index is outside uint32 range");
            }
            return static_cast<uint32_t>(index);
        }

        MakerOrder parse_maker_order(const json &item)
        {
            if (!item.is_object())
            {
                throw std::invalid_argument("maker order must be an object");
            }

            MakerOrder order;
            order.order_id = required_string(item, "order_id", "maker order");
            order.owner = required_string(item, "owner", "maker order");
            order.maker_address = required_string(item, "maker_address", "maker order");
            order.matched_amount = required_string(item, "matched_amount", "maker order");
            order.price = required_string(item, "price", "maker order");
            order.fee_rate_bps = required_string(item, "fee_rate_bps", "maker order");
            order.asset_id = required_string(item, "asset_id", "maker order");
            order.outcome = required_string(item, "outcome", "maker order");
            order.side = required_string(item, "side", "maker order");
            return order;
        }

        std::vector<MakerOrder> parse_maker_orders(const json &trade)
        {
            const auto orders = trade.find("maker_orders");
            if (orders == trade.end() || orders->is_null())
                return {};
            if (!orders->is_array())
            {
                throw std::invalid_argument("trade maker_orders must be an array or null");
            }

            std::vector<MakerOrder> result;
            result.reserve(orders->size());
            for (const auto &item : *orders)
                result.push_back(parse_maker_order(item));
            return result;
        }

        std::string transaction_hash(const json &trade)
        {
            const auto hash = trade.find("transaction_hash");
            if (hash == trade.end())
                return {};
            if (!hash->is_string())
            {
                throw std::invalid_argument(
                    "trade transaction_hash must be a string when present");
            }
            return hash->get<std::string>();
        }

        std::optional<std::string> error_message(const json &trade)
        {
            const auto error = trade.find("error_msg");
            if (error == trade.end() || error->is_null())
                return std::nullopt;
            if (!error->is_string())
            {
                throw std::invalid_argument(
                    "trade error_msg must be a string or null");
            }
            return error->get<std::string>();
        }

        Trade parse_trade(const json &item)
        {
            if (!item.is_object())
                throw std::invalid_argument("trade must be an object");

            Trade trade;
            trade.id = required_string(item, "id", "trade");
            trade.taker_order_id = required_string(item, "taker_order_id", "trade");
            trade.market = required_string(item, "market", "trade");
            trade.asset_id = required_string(item, "asset_id", "trade");
            trade.side = required_string(item, "side", "trade");
            trade.size = required_string(item, "size", "trade");
            trade.fee_rate_bps = required_string(item, "fee_rate_bps", "trade");
            trade.price = required_string(item, "price", "trade");
            trade.status = required_string(item, "status", "trade");
            trade.match_time = required_string(item, "match_time", "trade");
            trade.last_update = required_string(item, "last_update", "trade");
            trade.outcome = required_string(item, "outcome", "trade");
            trade.bucket_index = required_bucket_index(item);
            trade.owner = required_string(item, "owner", "trade");
            trade.maker_address = required_string(item, "maker_address", "trade");
            trade.maker_orders = parse_maker_orders(item);
            trade.transaction_hash = transaction_hash(item);
            trade.trader_side = required_string(item, "trader_side", "trade");
            trade.error_msg = error_message(item);
            return trade;
        }

        std::vector<Trade> parse_trade_array(const json &items)
        {
            if (!items.is_array())
                throw std::invalid_argument("trade page data must be an array");

            std::vector<Trade> trades;
            trades.reserve(items.size());
            for (const auto &item : items)
                trades.push_back(parse_trade(item));
            return trades;
        }
    }

    ParsedTradePage parse_trade_page_json(const std::string &json_text)
    {
        const auto parsed = json::parse(json_text);
        if (!parsed.is_object())
            throw std::invalid_argument("trade response must be an object");

        const auto &cursor = parsed.at("next_cursor");
        if (!cursor.is_string())
            throw std::invalid_argument("trade next_cursor must be a string");

        return {parse_trade_array(parsed.at("data")), cursor.get<std::string>()};
    }
}
