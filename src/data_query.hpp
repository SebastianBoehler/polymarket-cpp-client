#pragma once

#include "polymarket/data_requests.hpp"
#include "query_builder.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// Data API v2 query strings and argument checks, following py-sdk
// _internal/actions/data.py and data_params.py. Invalid input throws
// std::invalid_argument.
namespace polymarket::detail
{
    void data_check_selectors(const std::vector<std::string> &condition_ids,
                              const std::vector<std::int64_t> &event_ids);
    void data_require_user(const std::string &user);
    // Positive and no later than 9999-12-31T23:59:59Z.
    std::optional<std::int64_t> data_timestamp(std::optional<std::int64_t> value);
    void data_check_amount(const char *name, const std::optional<double> &value);

    // Condition IDs as given, without case-insensitive repeats; 31- or 32-byte hex, at most 20.
    std::vector<std::string> data_condition_ids(const std::vector<std::string> &values);
    // Lowercased market condition IDs without repeats, 31-byte structural IDs padded; at most 20.
    std::vector<std::string> data_market_condition_ids(const std::vector<std::string> &values);
    // Event IDs without repeats; each a positive 32-bit integer.
    std::vector<std::int64_t> data_event_ids(const std::vector<std::int64_t> &values);
    // `full_history` becomes start=1 with no end.
    std::pair<std::optional<std::int64_t>, std::optional<std::int64_t>>
    data_time_window(std::optional<std::int64_t> start, std::optional<std::int64_t> end,
                     bool full_history);

    std::string data_trade_query(const DataTradeFilter &filter);
    std::string data_activity_query(const std::string &user, const DataActivityFilter &filter);

    std::string data_position_query(const DataPositionFilter &filter);
    std::string data_portfolio_value_query(const std::string &user,
                                           const std::vector<std::string> &condition_ids);
    std::string data_user_query(const std::string &user);
    std::string data_user_pnl_query(const std::string &user, const std::string &interval,
                                    const std::string &fidelity);
    std::string data_user_volume_query(const std::string &user, const DataTimeRange &range);

    inline constexpr std::size_t data_price_history_page_size = 10000;

    std::string data_holders_query(const std::vector<std::string> &condition_ids,
                                   const DataHolderFilter &filter);
    std::string data_open_interest_query(const std::vector<std::string> &condition_ids);
    std::string data_live_volume_query(const std::vector<std::int64_t> &event_ids);
    // `now` (Unix seconds) ends a window without `end`.
    std::string data_price_history_query(const std::string &asset_id,
                                         const DataPriceHistoryFilter &filter,
                                         std::size_t page_size, std::int64_t now);
    std::string data_resolution_query(const DataResolutionFilter &filter);

    std::string data_trader_leaderboard_query(const DataLeaderboardFilter &filter);
    std::string data_trader_standing_query(const std::string &user,
                                           const DataLeaderboardFilter &filter);
    std::string data_biggest_winners_query(const DataLeaderboardFilter &filter);
} // namespace polymarket::detail
