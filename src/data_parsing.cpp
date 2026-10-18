#include "data_parsing.hpp"

#include "market_time.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

// Field names and rules follow py-sdk _internal/data_envelope.py and
// models/data/activity.py and common.py.
namespace polymarket::detail
{
    namespace
    {
        constexpr double max_epoch_seconds = 253402300799.0; // 9999-12-31T23:59:59Z

        std::string order_side(const json &item, const char *key)
        {
            auto side = field_required_string(item, key);
            if (side != "BUY" && side != "SELL") throw_bad_field(key, "BUY or SELL");
            return side;
        }

        void profile_fields(const json &item, DataActivity &activity)
        {
            activity.name = field_string(item, "name");
            activity.pseudonym = field_string(item, "pseudonym");
            activity.bio = field_string(item, "bio");
            activity.profile_image = field_string(item, "profile_image");
            activity.profile_image_optimized = field_string(item, "profile_image_optimized");
        }

        void market_text(const json &item, DataActivity &activity)
        {
            activity.title = field_string(item, "title");
            activity.slug = field_string(item, "slug");
            activity.icon = field_string(item, "icon");
            activity.event_slug = field_string(item, "event_slug");
        }

        bool is_market_event(std::string_view type)
        {
            return type == "SPLIT" || type == "MERGE" || type == "REDEEM" || type == "CONVERSION";
        }

        bool is_account_credit(std::string_view type)
        {
            return type == "REWARD" || type == "DEPOSIT" || type == "WITHDRAWAL" ||
                   type == "MAKER_REBATE" || type == "TAKER_REBATE" || type == "REFERRAL_REWARD" ||
                   type == "MIGRATION" || type == "TIP" || type == "YIELD";
        }
    } // namespace

    const json &data_page_items(const json &body, std::string &next_cursor)
    {
        if (!body.is_object()) throw std::invalid_argument("Data API page must be an object");
        const auto data = body.find("data");
        const auto pagination = body.find("pagination");
        if (data == body.end() || !data->is_array() || pagination == body.end() ||
            !pagination->is_object())
            throw std::invalid_argument("Data API page requires data and pagination");
        const auto has_more = pagination->find("has_more");
        const auto cursor = pagination->find("next_cursor");
        if (has_more == pagination->end() || !has_more->is_boolean() || cursor == pagination->end())
            throw std::invalid_argument("Data API page has invalid pagination metadata");
        if (!cursor->is_null() &&
            (!cursor->is_string() || cursor->get_ref<const std::string &>().empty()))
            throw std::invalid_argument("Data API page has an invalid next_cursor");
        if (has_more->get<bool>() == cursor->is_null())
            throw std::invalid_argument("Data API page has_more and next_cursor disagree");
        next_cursor = cursor->is_null() ? std::string() : cursor->get<std::string>();
        return *data;
    }

    const json &data_envelope(const json &body)
    {
        if (!body.is_object() || !body.contains("data"))
            throw std::invalid_argument("Data API response is missing data");
        return body.at("data");
    }

    std::string data_required_hex(const json &object, const char *key)
    {
        auto text = field_hex(object, key);
        if (text.empty()) throw_bad_field(key, "a 0x-prefixed hex string");
        return text;
    }

    double data_required_number(const json &object, const char *key)
    {
        const auto number = field_number(object, key);
        if (!number) throw_bad_field(key, "a number");
        return *number;
    }

    bool data_required_bool(const json &object, const char *key)
    {
        const auto value = field_bool(object, key);
        if (!value) throw_bad_field(key, "a boolean");
        return *value;
    }

    std::int64_t data_required_integer(const json &object, const char *key)
    {
        const auto value = field_integer(object, key);
        if (!value) throw_bad_field(key, "an integer");
        return *value;
    }

    std::string data_choice(const json &object, const char *key,
                            std::initializer_list<std::string_view> choices, const char *expected)
    {
        auto value = field_required_string(object, key);
        for (const auto allowed : choices)
        {
            if (value == allowed) return value;
        }
        throw_bad_field(key, expected);
    }

    std::string data_optional_choice(const json &object, const char *key,
                                     std::initializer_list<std::string_view> choices,
                                     const char *expected)
    {
        if (find_field(object, key) == nullptr) return {};
        return data_choice(object, key, choices, expected);
    }

    std::int64_t data_epoch_or_iso(const json &object, const char *key)
    {
        const auto *value = find_field(object, key);
        if (value != nullptr && value->is_string())
        {
            const auto &text = value->get_ref<const std::string &>();
            if (!text.empty() && text.find_first_not_of("0123456789") != std::string::npos)
            {
                const auto ms = parse_iso8601_ms(text);
                if (ms == 0) throw_bad_field(key, "an ISO 8601 UTC time or Unix seconds");
                return static_cast<std::int64_t>(ms / 1000);
            }
        }
        return data_epoch_seconds(object, key);
    }

    std::optional<std::int64_t> data_optional_epoch_or_iso(const json &object, const char *key)
    {
        const auto *value = find_field(object, key);
        if (value == nullptr || *value == "") return std::nullopt;
        return data_epoch_or_iso(object, key);
    }

    std::int64_t data_epoch_seconds(const json &object, const char *key)
    {
        const auto seconds = field_number(object, key);
        if (!seconds || std::abs(*seconds) > max_epoch_seconds)
            throw_bad_field(key, "Unix seconds");
        return static_cast<std::int64_t>(std::floor(*seconds));
    }

    std::optional<std::int64_t> data_optional_epoch_seconds(const json &object, const char *key)
    {
        const auto *time = find_field(object, key);
        if (time == nullptr || *time == 0 || *time == "0" || *time == "") return std::nullopt;
        return data_epoch_seconds(object, key);
    }

    std::optional<int> data_outcome_index(const json &object, const char *key)
    {
        const auto index = field_integer(object, key);
        if (!index || *index == 999) return std::nullopt;
        if (*index < std::numeric_limits<int>::min() || *index > std::numeric_limits<int>::max())
            throw_bad_field(key, "an outcome index");
        return static_cast<int>(*index);
    }

    std::string data_combo_condition_id(const json &object, const char *key)
    {
        auto id = data_required_hex(object, key);
        std::transform(id.begin(), id.end(), id.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (id.size() == 66 && id.starts_with("0x03") && (id.ends_with("00") || id.ends_with("01")))
            id.resize(64);
        if (id.size() != 64 || !id.starts_with("0x03"))
            throw_bad_field(key, "a protocol v2 combo condition ID");
        return id;
    }

    DataTrade parse_data_trade(const json &item)
    {
        DataTrade trade;
        trade.wallet = field_required_string(item, "proxy_wallet");
        trade.asset_id = field_required_string(item, "token_id");
        trade.condition_id = data_required_hex(item, "condition_id");
        trade.side = order_side(item, "side");
        trade.size = data_required_number(item, "size");
        trade.price = data_required_number(item, "price");
        trade.timestamp = data_epoch_seconds(item, "timestamp");
        trade.transaction_hash = field_required_string(item, "transaction_hash");
        trade.title = field_string(item, "title");
        trade.slug = field_string(item, "slug");
        trade.icon = field_string(item, "icon");
        trade.event_slug = field_string(item, "event_slug");
        trade.outcome = field_string(item, "outcome");
        trade.outcome_index = data_outcome_index(item, "outcome_index");
        trade.name = field_string(item, "name");
        trade.pseudonym = field_string(item, "pseudonym");
        trade.bio = field_string(item, "bio");
        trade.profile_image = field_string(item, "profile_image");
        trade.profile_image_optimized = field_string(item, "profile_image_optimized");
        trade.raw = item;
        return trade;
    }

    DataActivity parse_data_activity(const json &item)
    {
        if (!item.is_object()) throw std::invalid_argument("Data API activity must be an object");
        DataActivity activity;
        const auto *type = find_field(item, "type");
        activity.type =
            type != nullptr && type->is_string() ? type->get<std::string>() : std::string();
        activity.is_combo = field_bool(item, "is_combo").value_or(false);
        activity.raw = item;
        profile_fields(item, activity);

        const bool trade = activity.type == "TRADE";
        activity.known =
            trade || is_market_event(activity.type) || is_account_credit(activity.type);
        if (!activity.known)
        {
            activity.wallet = field_string(item, "proxy_wallet");
            activity.transaction_hash = field_string(item, "transaction_hash");
            activity.timestamp = data_optional_epoch_seconds(item, "timestamp");
            return activity;
        }

        activity.wallet = field_required_string(item, "proxy_wallet");
        activity.timestamp = data_epoch_seconds(item, "timestamp");
        activity.transaction_hash = field_required_string(item, "transaction_hash");
        if (trade)
        {
            activity.condition_id = activity.is_combo
                                        ? data_combo_condition_id(item, "condition_id")
                                        : data_required_hex(item, "condition_id");
            activity.asset_id = field_required_string(item, "token_id");
            activity.side = order_side(item, "side");
            activity.shares = data_required_number(item, "size");
            activity.amount = data_required_number(item, "usdc_size");
            activity.price = data_required_number(item, "price");
            activity.title = field_string(item, "title");
            activity.icon = field_string(item, "icon");
            if (!activity.is_combo)
            {
                activity.slug = field_string(item, "slug");
                activity.event_slug = field_string(item, "event_slug");
                activity.outcome = field_string(item, "outcome");
                activity.outcome_index = data_outcome_index(item, "outcome_index");
            }
            return activity;
        }
        activity.amount = data_required_number(item, "usdc_size");
        if (is_market_event(activity.type))
        {
            activity.condition_id = data_required_hex(item, "condition_id");
            market_text(item, activity);
        }
        else if (activity.type == "TIP")
        {
            activity.tip_side = field_string(item, "side");
            if (!activity.tip_side.empty() && activity.tip_side != "IN" &&
                activity.tip_side != "OUT")
                throw_bad_field("side", "IN or OUT");
        }
        return activity;
    }

    Page<DataTrade> parse_data_trade_page(const std::string &body)
    {
        return data_page<DataTrade>(body, parse_data_trade);
    }

    Page<DataActivity> parse_data_activity_page(const std::string &body)
    {
        return data_page<DataActivity>(body, parse_data_activity);
    }
} // namespace polymarket::detail
