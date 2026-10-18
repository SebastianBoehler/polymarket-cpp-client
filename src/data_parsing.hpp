#pragma once

#include "json_fields.hpp"
#include "polymarket/data_analytics_types.hpp"
#include "polymarket/data_leaderboard_types.hpp"
#include "polymarket/data_portfolio_types.hpp"
#include "polymarket/data_types.hpp"
#include "polymarket/pagination.hpp"

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Data API v2 response parsing. Malformed input throws std::exception.
namespace polymarket::detail
{
    // The `data` array of a `{data, pagination: {has_more, next_cursor}}` page;
    // sets `next_cursor` ("" on the last page).
    const json &data_page_items(const json &body, std::string &next_cursor);
    // The `data` member of a `{data}` envelope; it may be null.
    const json &data_envelope(const json &body);

    template <typename T, typename Parse>
    Page<T> data_page(const std::string &body, Parse parse_item)
    {
        const auto parsed = json::parse(body);
        Page<T> page;
        const auto &items = data_page_items(parsed, page.next_cursor);
        page.items.reserve(items.size());
        for (const auto &item : items)
            page.items.push_back(parse_item(item));
        return page;
    }

    std::string data_required_hex(const json &object, const char *key);
    double data_required_number(const json &object, const char *key);
    bool data_required_bool(const json &object, const char *key);
    std::int64_t data_required_integer(const json &object, const char *key);
    // One of `choices`; the optional form reads absent or null as "".
    std::string data_choice(const json &object, const char *key,
                            std::initializer_list<std::string_view> choices, const char *expected);
    std::string data_optional_choice(const json &object, const char *key,
                                     std::initializer_list<std::string_view> choices,
                                     const char *expected);

    // Unix seconds from a number or numeric string.
    std::int64_t data_epoch_seconds(const json &object, const char *key);
    // Unset for an absent, null, 0, "0" or "" value.
    std::optional<std::int64_t> data_optional_epoch_seconds(const json &object, const char *key);
    // Unix seconds from an ISO 8601 UTC string, a number or a numeric string;
    // the optional form reads absent, null and "" as unset.
    std::int64_t data_epoch_or_iso(const json &object, const char *key);
    std::optional<std::int64_t> data_optional_epoch_or_iso(const json &object, const char *key);
    // Unset for 999, the Data API's "no outcome".
    std::optional<int> data_outcome_index(const json &object, const char *key);
    // A combo condition ID in its 31-byte form, lowercased.
    std::string data_combo_condition_id(const json &object, const char *key);

    DataTrade parse_data_trade(const json &item);
    DataActivity parse_data_activity(const json &item);
    Page<DataTrade> parse_data_trade_page(const std::string &body);
    Page<DataActivity> parse_data_activity_page(const std::string &body);

    DataPosition parse_data_position(const json &item);
    DataPnlPoint parse_data_pnl_point(const json &item);
    Page<DataPosition> parse_data_position_page(const std::string &body);
    DataPortfolioValue parse_data_portfolio_value(const std::string &body);
    std::optional<DataUserStats> parse_data_user_stats(const std::string &body);
    DataPnlSeries parse_data_pnl_series(const std::string &body);
    DataUserVolume parse_data_user_volume(const std::string &body);

    DataHolder parse_data_holder(const json &item);
    DataResolution parse_data_resolution(const json &item);
    Page<DataAssetHolders> parse_data_holders_page(const std::string &body);
    std::vector<DataOpenInterest> parse_data_open_interests(const std::string &body);
    DataLiveVolume parse_data_live_volume(const std::string &body);
    Page<DataPricePoint> parse_data_price_history_page(const std::string &body);
    std::vector<DataResolution> parse_data_resolutions(const std::string &body);

    DataTraderEntry parse_data_trader_entry(const json &item);
    DataBiggestWinner parse_data_biggest_winner(const json &item);
    Page<DataTraderEntry> parse_data_trader_leaderboard_page(const std::string &body);
    std::optional<DataTraderStanding> parse_data_trader_standing(const std::string &body);
    Page<DataBiggestWinner> parse_data_biggest_winners_page(const std::string &body);
} // namespace polymarket::detail
