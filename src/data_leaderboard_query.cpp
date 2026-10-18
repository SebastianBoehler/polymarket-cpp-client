#include "data_query.hpp"

#include <stdexcept>
#include <string>

// Leaderboard queries, following py-sdk _internal/actions/data.py.
namespace polymarket::detail
{
    namespace
    {
        void check_window(const DataLeaderboardFilter &filter)
        {
            check_choice("window", filter.window, {"day", "week", "month", "all"});
        }

        void reject_sort(const DataLeaderboardFilter &filter)
        {
            if (!filter.sort_by.empty())
                throw std::invalid_argument("sort_by applies only to the trader leaderboard");
        }
    } // namespace

    std::string data_trader_leaderboard_query(const DataLeaderboardFilter &filter)
    {
        check_window(filter);
        check_choice("sort_by", filter.sort_by, {"PNL", "VOLUME"});
        QueryBuilder query;
        query.add("category", filter.category);
        query.add("time_period", filter.window);
        query.add("sort_by", filter.sort_by);
        return query.str();
    }

    std::string data_trader_standing_query(const std::string &user,
                                           const DataLeaderboardFilter &filter)
    {
        check_window(filter);
        data_require_user(user);
        reject_sort(filter);
        QueryBuilder query;
        query.add("user", user);
        query.add("category", filter.category);
        query.add("time_period", filter.window);
        return query.str();
    }

    std::string data_biggest_winners_query(const DataLeaderboardFilter &filter)
    {
        check_window(filter);
        reject_sort(filter);
        QueryBuilder query;
        query.add("category", filter.category);
        query.add("time_period", filter.window);
        return query.str();
    }
} // namespace polymarket::detail
