#pragma once

#include "polymarket/data_requests.hpp"
#include "query_builder.hpp"

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
    // Condition IDs as given, without case-insensitive repeats; 31- or 32-byte hex, at most 20.
    std::vector<std::string> data_condition_ids(const std::vector<std::string> &values);
    // Event IDs without repeats; each a positive 32-bit integer.
    std::vector<std::int64_t> data_event_ids(const std::vector<std::int64_t> &values);
    // `full_history` becomes start=1 with no end.
    std::pair<std::optional<std::int64_t>, std::optional<std::int64_t>>
    data_time_window(std::optional<std::int64_t> start, std::optional<std::int64_t> end,
                     bool full_history);

    std::string data_trade_query(const DataTradeFilter &filter);
    std::string data_activity_query(const std::string &user, const DataActivityFilter &filter);
} // namespace polymarket::detail
