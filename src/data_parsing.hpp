#pragma once

#include "json_fields.hpp"
#include "polymarket/data_types.hpp"
#include "polymarket/pagination.hpp"

#include <cstdint>
#include <optional>
#include <string>

// Data API v2 response parsing. Malformed input throws std::exception.
namespace polymarket::detail
{
    // The `data` array of a `{data, pagination: {has_more, next_cursor}}` page;
    // sets `next_cursor` ("" on the last page).
    const json &data_page_items(const json &body, std::string &next_cursor);
    // The `data` member of a `{data}` envelope; it may be null.
    const json &data_envelope(const json &body);

    // Unix seconds from a number or numeric string.
    std::int64_t data_epoch_seconds(const json &object, const char *key);
    // Unset for 999, the Data API's "no outcome".
    std::optional<int> data_outcome_index(const json &object, const char *key);
    // A combo condition ID in its 31-byte form, lowercased.
    std::string data_combo_condition_id(const json &object, const char *key);

    DataTrade parse_data_trade(const json &item);
    DataActivity parse_data_activity(const json &item);
    Page<DataTrade> parse_data_trade_page(const std::string &body);
    Page<DataActivity> parse_data_activity_page(const std::string &body);
} // namespace polymarket::detail
