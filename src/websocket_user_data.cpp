#include "user_stream_protocol.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <stdexcept>

using json = nlohmann::json;

namespace polymarket::detail
{
    namespace
    {
        std::string text_field(const json &item, const char *field,
                               const char *context, bool required)
        {
            const auto value = item.find(field);
            if (value == item.end() || value->is_null())
            {
                if (required)
                {
                    throw std::invalid_argument(std::string(context) + " requires " +
                                                field);
                }
                return {};
            }
            if (value->is_string()) return value->get<std::string>();
            if (value->is_number()) return value->dump();
            throw std::invalid_argument(std::string(context) + " " + field +
                                        " must be a string or number");
        }

        std::string required_text(const json &item, const char *field,
                                  const char *context)
        {
            auto value = text_field(item, field, context, true);
            if (value.empty())
            {
                throw std::invalid_argument(std::string(context) + " " + field +
                                            " must not be empty");
            }
            return value;
        }

        std::string optional_text(const json &item, const char *field,
                                  const char *context)
        {
            return text_field(item, field, context, false);
        }

        std::string asset_id(const json &item, const char *context)
        {
            if (item.contains("asset_id")) return required_text(item, "asset_id", context);
            return required_text(item, "token_id", context);
        }

        std::string uppercase(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(),
                           [](unsigned char c)
                           { return static_cast<char>(std::toupper(c)); });
            return value;
        }

        std::string side(const json &item, const char *field, const char *context,
                         bool required)
        {
            auto value = uppercase(text_field(item, field, context, required));
            if (required && value != "BUY" && value != "SELL")
            {
                throw std::invalid_argument(std::string(context) + " " + field +
                                            " must be BUY or SELL");
            }
            return value;
        }

        std::vector<std::string> string_array(const json &item, const char *field,
                                              const char *context)
        {
            const auto value = item.find(field);
            if (value == item.end() || value->is_null()) return {};
            if (!value->is_array())
            {
                throw std::invalid_argument(std::string(context) + " " + field +
                                            " must be an array or null");
            }
            std::vector<std::string> result;
            result.reserve(value->size());
            for (const auto &entry : *value)
            {
                if (!entry.is_string())
                {
                    throw std::invalid_argument(std::string(context) + " " + field +
                                                " entries must be strings");
                }
                result.push_back(entry.get<std::string>());
            }
            return result;
        }

        std::optional<uint32_t> bucket_index(const json &item)
        {
            const auto value = item.find("bucket_index");
            if (value == item.end() || value->is_null()) return std::nullopt;
            if (value->is_number_unsigned())
            {
                const auto index = value->get<uint64_t>();
                if (index <= std::numeric_limits<uint32_t>::max())
                    return static_cast<uint32_t>(index);
            }
            else if (value->is_number_integer())
            {
                const auto index = value->get<int64_t>();
                if (index >= 0 && index <= std::numeric_limits<uint32_t>::max())
                    return static_cast<uint32_t>(index);
            }
            throw std::invalid_argument(
                "user trade bucket_index must be a uint32 integer");
        }

        MakerOrder parse_maker_order(const json &item)
        {
            constexpr const char *context = "user trade maker order";
            if (!item.is_object())
                throw std::invalid_argument("user trade maker order must be an object");
            MakerOrder order;
            order.order_id = required_text(item, "order_id", context);
            order.owner = required_text(item, "owner", context);
            order.maker_address = optional_text(item, "maker_address", context);
            order.matched_amount = required_text(item, "matched_amount", context);
            order.price = required_text(item, "price", context);
            order.fee_rate_bps = optional_text(item, "fee_rate_bps", context);
            order.asset_id = asset_id(item, context);
            order.outcome = optional_text(item, "outcome", context);
            order.side = side(item, "side", context, true);
            return order;
        }

        std::vector<MakerOrder> maker_orders(const json &item)
        {
            const auto orders = item.find("maker_orders");
            if (orders == item.end() || orders->is_null()) return {};
            if (!orders->is_array())
            {
                throw std::invalid_argument(
                    "user trade maker_orders must be an array or null");
            }
            std::vector<MakerOrder> result;
            result.reserve(orders->size());
            for (const auto &order : *orders)
                result.push_back(parse_maker_order(order));
            return result;
        }

        UserOrderEvent parse_order(const json &item)
        {
            constexpr const char *context = "user order";
            UserOrderEvent order;
            order.id = required_text(item, "id", context);
            order.owner = required_text(item, "owner", context);
            order.market = required_text(item, "market", context);
            order.asset_id = asset_id(item, context);
            order.side = side(item, "side", context, true);
            order.original_size = required_text(item, "original_size", context);
            order.size_matched = required_text(item, "size_matched", context);
            order.price = required_text(item, "price", context);
            order.type = uppercase(required_text(item, "type", context));
            order.status = optional_text(item, "status", context);
            order.order_type = optional_text(item, "order_type", context);
            order.maker_address = optional_text(item, "maker_address", context);
            order.order_owner = optional_text(item, "order_owner", context);
            order.associate_trades = string_array(item, "associate_trades", context);
            order.outcome = optional_text(item, "outcome", context);
            order.created_at = optional_text(item, "created_at", context);
            order.expiration = optional_text(item, "expiration", context);
            order.timestamp = optional_text(item, "timestamp", context);
            return order;
        }

        UserTradeEvent parse_trade(const json &item)
        {
            constexpr const char *context = "user trade";
            UserTradeEvent trade;
            trade.id = required_text(item, "id", context);
            trade.taker_order_id = required_text(item, "taker_order_id", context);
            trade.market = required_text(item, "market", context);
            trade.asset_id = asset_id(item, context);
            trade.side = side(item, "side", context, true);
            trade.size = required_text(item, "size", context);
            trade.price = required_text(item, "price", context);
            trade.status = required_text(item, "status", context);
            trade.owner = required_text(item, "owner", context);
            trade.fee_rate_bps = optional_text(item, "fee_rate_bps", context);
            trade.match_time = item.contains("match_time")
                                   ? optional_text(item, "match_time", context)
                                   : optional_text(item, "matchtime", context);
            trade.last_update = optional_text(item, "last_update", context);
            trade.timestamp = optional_text(item, "timestamp", context);
            trade.trade_owner = optional_text(item, "trade_owner", context);
            trade.maker_address = optional_text(item, "maker_address", context);
            trade.transaction_hash = optional_text(item, "transaction_hash", context);
            trade.bucket_index = bucket_index(item);
            trade.maker_orders = maker_orders(item);
            trade.trader_side = side(item, "trader_side", context, false);
            trade.outcome = optional_text(item, "outcome", context);
            return trade;
        }

        std::vector<std::string> difference(const std::vector<std::string> &left,
                                            const std::vector<std::string> &right)
        {
            std::vector<std::string> result;
            for (const auto &value : left)
            {
                if (std::find(right.begin(), right.end(), value) == right.end())
                    result.push_back(value);
            }
            return result;
        }
    }

    std::string user_subscription_message(const UserSubscriptionState &state,
                                          const ApiCredentials &credentials)
    {
        json message{{"auth", {{"apiKey", credentials.api_key},
                               {"secret", credentials.api_secret},
                               {"passphrase", credentials.api_passphrase}}},
                     {"type", "user"}};
        if (!state.all_markets)
        {
            message["markets"] = state.markets;
        }
        return message.dump();
    }

    std::string user_subscription_update_message(const std::vector<std::string> &markets,
                                                 bool subscribe)
    {
        return json{{"markets", markets},
                    {"operation", subscribe ? "subscribe" : "unsubscribe"}}
            .dump();
    }

    UserSubscriptionSync plan_user_subscription_sync(const UserSubscriptionState &before,
                                                     const UserSubscriptionState &after,
                                                     const ApiCredentials &credentials)
    {
        UserSubscriptionSync sync;
        if (!after.active())
        {
            // An empty market list means "all markets" to the server, so the
            // last unsubscribe must drop the authenticated session instead.
            sync.reconnect = before.active();
            return sync;
        }
        if (!before.active())
        {
            sync.messages.push_back(user_subscription_message(after, credentials));
            return sync;
        }
        if (after.all_markets)
        {
            if (!before.all_markets)
                sync.messages.push_back(
                    user_subscription_update_message(before.markets, false));
            return sync;
        }
        if (before.all_markets)
        {
            sync.messages.push_back(
                user_subscription_update_message(after.markets, true));
            return sync;
        }

        const auto added = difference(after.markets, before.markets);
        const auto removed = difference(before.markets, after.markets);
        if (!added.empty())
            sync.messages.push_back(user_subscription_update_message(added, true));
        if (!removed.empty())
            sync.messages.push_back(user_subscription_update_message(removed, false));
        return sync;
    }

    std::vector<UserStreamEvent> parse_user_events(const std::string &message)
    {
        const auto parsed = json::parse(message);
        const auto items = parsed.is_array() ? parsed : json::array({parsed});
        std::vector<UserStreamEvent> events;
        events.reserve(items.size());

        for (const auto &item : items)
        {
            if (!item.is_object())
                throw std::invalid_argument("user event must be an object");

            // Accept both the wire shape ({"event_type":"order",...}) and the
            // normalized envelope ({"topic":"user","type":"order","payload":{}}).
            const json *body = &item;
            std::string kind = item.value("event_type", "");
            if (kind.empty() && item.contains("payload") && item["payload"].is_object())
            {
                kind = item.value("type", "");
                body = &item["payload"];
            }

            if (kind == "order")
                events.emplace_back(parse_order(*body));
            else if (kind == "trade")
                events.emplace_back(parse_trade(*body));
        }
        return events;
    }
}
