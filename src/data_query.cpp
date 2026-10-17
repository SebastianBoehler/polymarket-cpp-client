#include "data_query.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>
#include <stdexcept>
#include <string>

namespace polymarket::detail
{
    namespace
    {
        constexpr std::int64_t max_epoch_seconds = 253402300799; // 9999-12-31T23:59:59Z
        constexpr std::size_t max_condition_ids = 20;

        bool is_hex(const std::string &value)
        {
            return value.size() > 2 && value.starts_with("0x") &&
                   value.find_first_not_of("0123456789abcdefABCDEF", 2) == std::string::npos;
        }

        template <typename Normalize>
        std::vector<std::string> distinct_condition_ids(const std::vector<std::string> &values,
                                                        Normalize normalize)
        {
            std::vector<std::string> unique;
            std::set<std::string> seen;
            for (const auto &value : values)
            {
                if (!is_hex(value)) throw std::invalid_argument("condition_id must be a hex string");
                auto id = normalize(value);
                std::string key = id;
                std::transform(key.begin(), key.end(), key.begin(),
                               [](unsigned char c) { return std::tolower(c); });
                if (seen.insert(key).second) unique.push_back(std::move(id));
            }
            if (unique.size() > max_condition_ids)
                throw std::invalid_argument("condition_id accepts at most 20 distinct values");
            return unique;
        }
    } // namespace

    void data_check_selectors(const std::vector<std::string> &condition_ids,
                              const std::vector<std::int64_t> &event_ids)
    {
        if (!condition_ids.empty() && !event_ids.empty())
            throw std::invalid_argument("Provide condition_id or event_id, not both");
    }

    void data_require_user(const std::string &user)
    {
        if (user.empty()) throw std::invalid_argument("user is required");
    }

    std::optional<std::int64_t> data_timestamp(std::optional<std::int64_t> value)
    {
        if (value && (*value <= 0 || *value > max_epoch_seconds))
            throw std::invalid_argument(
                "Timestamp must be positive and no later than 9999-12-31T23:59:59Z");
        return value;
    }

    void data_check_amount(const char *name, const std::optional<double> &value)
    {
        if (value && (!std::isfinite(*value) || *value < 0))
            throw std::invalid_argument(std::string(name) + " must be a finite non-negative amount");
    }

    std::vector<std::string> data_condition_ids(const std::vector<std::string> &values)
    {
        return distinct_condition_ids(
            values,
            [](const std::string &value)
            {
                if (value.size() != 64 && value.size() != 66)
                    throw std::invalid_argument(
                        "Expected a 31-byte or 32-byte hex string, received: " + value);
                return value;
            });
    }

    std::vector<std::string> data_market_condition_ids(const std::vector<std::string> &values)
    {
        return distinct_condition_ids(
            values,
            [](const std::string &value)
            {
                std::string id = value;
                std::transform(id.begin(), id.end(), id.begin(),
                               [](unsigned char c) { return std::tolower(c); });
                if (id.size() == 64 && (id.starts_with("0x01") || id.starts_with("0x02")))
                    id += "00";
                if (id.size() != 66)
                    throw std::invalid_argument(
                        "Expected a 32-byte condition ID or a 31-byte structural market ID");
                return id;
            });
    }

    std::vector<std::int64_t> data_event_ids(const std::vector<std::int64_t> &values)
    {
        std::vector<std::int64_t> unique;
        for (const auto value : values)
        {
            if (value <= 0 || value > 2147483647)
                throw std::invalid_argument("event_ids must contain positive 32-bit integers");
            if (std::find(unique.begin(), unique.end(), value) == unique.end())
                unique.push_back(value);
        }
        return unique;
    }

    std::pair<std::optional<std::int64_t>, std::optional<std::int64_t>>
    data_time_window(std::optional<std::int64_t> start, std::optional<std::int64_t> end,
                     bool full_history)
    {
        if (full_history)
        {
            if (start || end)
                throw std::invalid_argument("full_history cannot be combined with start or end");
            return {std::int64_t{1}, std::nullopt};
        }
        return {data_timestamp(start), data_timestamp(end)};
    }

    std::string data_trade_query(const DataTradeFilter &filter)
    {
        check_choice("side", filter.side, {"BUY", "SELL"});
        check_choice("filter_type", filter.filter_type, {"CASH", "TOKENS"});
        data_check_selectors(filter.condition_ids, filter.event_ids);
        const auto event_ids = data_event_ids(filter.event_ids);
        const auto condition_ids = data_condition_ids(filter.condition_ids);
        const auto [start, end] = data_time_window(filter.start, filter.end, filter.full_history);
        data_check_amount("filter_amount", filter.filter_amount);

        QueryBuilder query;
        query.add("user", filter.user);
        query.add_joined("condition_id", condition_ids);
        query.add_joined("event_id", event_ids);
        query.add("side", filter.side);
        query.add("taker_only", filter.taker_only);
        query.add("filter_type", filter.filter_type);
        query.add("filter_amount", filter.filter_amount);
        query.add("start", start);
        query.add("end", end);
        return query.str();
    }

    std::string data_activity_query(const std::string &user, const DataActivityFilter &filter)
    {
        check_choice("side", filter.side, {"BUY", "SELL"});
        check_choice("sort_direction", filter.sort_direction, {"ASC", "DESC"});
        data_require_user(user);
        data_check_selectors(filter.condition_ids, filter.event_ids);
        const auto event_ids = data_event_ids(filter.event_ids);
        const auto condition_ids = data_condition_ids(filter.condition_ids);
        const auto [start, end] = data_time_window(filter.start, filter.end, filter.full_history);
        for (const auto &type : filter.types)
        {
            if (type.empty())
                throw std::invalid_argument("activity_types cannot contain an empty type");
            check_choice("activity_types", type,
                         {"TRADE", "SPLIT", "MERGE", "REDEEM", "REWARD", "CONVERSION", "MIGRATION",
                          "DEPOSIT", "WITHDRAWAL", "YIELD", "MAKER_REBATE", "TAKER_REBATE",
                          "REFERRAL_REWARD", "TIP"});
        }

        QueryBuilder query;
        query.add("user", user);
        query.add_joined("condition_id", condition_ids);
        query.add_joined("event_id", event_ids);
        query.add_joined("type", filter.types);
        query.add("side", filter.side);
        query.add("sort_direction", filter.sort_direction);
        query.add("start", start);
        query.add("end", end);
        query.add("exclude_deposits_withdrawals", std::optional<bool>(false));
        return query.str();
    }
} // namespace polymarket::detail
