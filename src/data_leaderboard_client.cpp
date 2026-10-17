#include "data_client_internal.hpp"
#include "data_parsing.hpp"
#include "data_query.hpp"

#include <string>

namespace polymarket
{
    Paginator<DataTraderEntry>
    DataClient::trader_leaderboard_pager(const DataLeaderboardFilter &filter, std::size_t page_size,
                                         const PageRequest &resume)
    {
        return cursor_pager<DataTraderEntry>(
            "/v2/leaderboard", page_size, detail::data_max_page_size, resume,
            [&] { return detail::data_trader_leaderboard_query(filter); },
            detail::parse_data_trader_leaderboard_page);
    }

    Result<std::optional<DataTraderStanding>>
    DataClient::get_trader_standing(const std::string &user, const DataLeaderboardFilter &filter)
    {
        return fetch<std::optional<DataTraderStanding>>(
            [&] { return "/v2/leaderboard" + detail::data_trader_standing_query(user, filter); },
            detail::parse_data_trader_standing);
    }

    Paginator<DataBiggestWinner>
    DataClient::biggest_winners_pager(const DataLeaderboardFilter &filter, std::size_t page_size,
                                      const PageRequest &resume)
    {
        return cursor_pager<DataBiggestWinner>(
            "/v2/biggest-winners", page_size, detail::data_max_page_size, resume,
            [&] { return detail::data_biggest_winners_query(filter); },
            detail::parse_data_biggest_winners_page);
    }
} // namespace polymarket
