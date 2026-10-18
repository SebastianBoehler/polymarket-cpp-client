#include "data_client_internal.hpp"
#include "data_parsing.hpp"
#include "data_query.hpp"

#include <string>

namespace polymarket
{
    Paginator<DataPosition> DataClient::positions_pager(const DataPositionFilter &filter,
                                                        std::size_t page_size,
                                                        const PageRequest &resume)
    {
        return cursor_pager<DataPosition>(
            "/v2/positions", page_size, detail::data_max_page_size, resume,
            [&] { return detail::data_position_query(filter); }, detail::parse_data_position_page);
    }

    Result<DataPortfolioValue>
    DataClient::get_portfolio_value(const std::string &user,
                                    const std::vector<std::string> &condition_ids)
    {
        return fetch<DataPortfolioValue>(
            [&] { return "/v2/value" + detail::data_portfolio_value_query(user, condition_ids); },
            detail::parse_data_portfolio_value);
    }

    Result<std::optional<DataUserStats>> DataClient::get_user_stats(const std::string &user)
    {
        return fetch<std::optional<DataUserStats>>(
            [&] { return "/v2/user-stats" + detail::data_user_query(user); },
            detail::parse_data_user_stats);
    }

    Result<DataPnlSeries> DataClient::get_user_pnl(const std::string &user,
                                                   const std::string &interval,
                                                   const std::string &fidelity)
    {
        return fetch<DataPnlSeries>(
            [&] { return "/v2/user-pnl" + detail::data_user_pnl_query(user, interval, fidelity); },
            detail::parse_data_pnl_series);
    }

    Result<DataUserVolume> DataClient::get_user_volume(const std::string &user,
                                                       const DataTimeRange &range)
    {
        return fetch<DataUserVolume>(
            [&] { return "/v2/user-volume" + detail::data_user_volume_query(user, range); },
            detail::parse_data_user_volume);
    }

    Result<std::string> DataClient::download_accounting_snapshot(const std::string &user)
    {
        return fetch<std::string>(
            [&] { return "/v1/accounting/snapshot" + detail::data_user_query(user); },
            [](const std::string &body) { return body; });
    }
} // namespace polymarket
