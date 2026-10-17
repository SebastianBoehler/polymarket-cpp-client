#include "data_query.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>

// Portfolio queries, following py-sdk _internal/actions/data.py.
namespace polymarket::detail
{
    namespace
    {
        std::string checked_title(const std::string &title)
        {
            if (title.find_first_not_of(" \t\n\v\f\r") == std::string::npos) return {};
            const auto characters = std::count_if(title.begin(), title.end(),
                                                  [](char c) { return (c & 0xC0) != 0x80; });
            if (characters > 200)
                throw std::invalid_argument("title must contain at most 200 characters");
            return title;
        }
    } // namespace

    std::string data_position_query(const DataPositionFilter &filter)
    {
        check_choice("status", filter.status,
                     {"OPEN", "REDEEMABLE", "REDEEMABLE_LOST", "MERGEABLE", "CLOSED"});
        check_choice("filter_type", filter.filter_type, {"CASH", "TOKENS"});
        check_choice("sort_by", filter.sort_by,
                     {"CURRENT_VALUE", "PRICE", "TOKENS", "UNREALIZED_PNL", "REALIZED_PNL",
                      "TOTAL_PNL", "TIMESTAMP"});
        check_choice("sort_direction", filter.sort_direction, {"ASC", "DESC"});
        data_check_selectors(filter.condition_ids, filter.event_ids);
        const auto event_ids = data_event_ids(filter.event_ids);
        const auto condition_ids = data_market_condition_ids(filter.condition_ids);
        const auto start = data_timestamp(filter.start);
        const auto end = data_timestamp(filter.end);
        const bool has_user = !filter.user.empty();
        if (!has_user && condition_ids.size() != 1)
            throw std::invalid_argument("Provide user or exactly one condition_id");
        if (!event_ids.empty() && !has_user) throw std::invalid_argument("event_id requires user");
        if ((filter.status == "REDEEMABLE_LOST" || filter.status == "MERGEABLE") && !has_user)
            throw std::invalid_argument(filter.status + " requires user");
        if (filter.status == "CLOSED" && filter.include_archived.value_or(false))
            throw std::invalid_argument("include_archived is invalid with CLOSED");
        data_check_amount("filter_amount", filter.filter_amount);
        const auto title = checked_title(filter.title);

        QueryBuilder query;
        query.add("user", filter.user);
        query.add_joined("condition_id", condition_ids);
        query.add("status", filter.status);
        query.add_joined("event_id", event_ids);
        query.add("title", title);
        query.add("filter_type", filter.filter_type);
        query.add("filter_amount", filter.filter_amount);
        query.add("include_archived", filter.include_archived);
        query.add("sort_by", filter.sort_by);
        query.add("sort_direction", filter.sort_direction);
        query.add("start", start);
        query.add("end", end);
        return query.str();
    }

    std::string data_portfolio_value_query(const std::string &user,
                                           const std::vector<std::string> &condition_ids)
    {
        data_require_user(user);
        const auto ids = data_market_condition_ids(condition_ids);
        QueryBuilder query;
        query.add("user", user);
        query.add_joined("condition_id", ids);
        return query.str();
    }

    std::string data_user_query(const std::string &user)
    {
        data_require_user(user);
        QueryBuilder query;
        query.add("user", user);
        return query.str();
    }

    std::string data_user_pnl_query(const std::string &user, const std::string &interval,
                                    const std::string &fidelity)
    {
        check_choice("interval", interval, {"max", "all", "1m", "1w", "1d", "12h", "6h"});
        check_choice("fidelity", fidelity, {"1d", "18h", "12h", "3h", "1h"});
        data_require_user(user);
        QueryBuilder query;
        query.add("user", user);
        query.add("interval", interval);
        query.add("fidelity", fidelity);
        return query.str();
    }

    std::string data_user_volume_query(const std::string &user, const DataTimeRange &range)
    {
        data_require_user(user);
        const auto [start, end] = data_time_window(range.start, range.end, range.full_history);
        QueryBuilder query;
        query.add("user", user);
        query.add("start", start);
        query.add("end", end);
        return query.str();
    }
} // namespace polymarket::detail
