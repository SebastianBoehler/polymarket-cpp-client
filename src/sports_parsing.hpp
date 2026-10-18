#pragma once

#include "json_fields.hpp"
#include "polymarket/sports_stream.hpp"

#include <cstdint>
#include <optional>
#include <string>

// Sports WebSocket parsing, following py-sdk models/sports_events.py.
// Malformed input throws std::exception.
namespace polymarket::detail
{
    // Unix milliseconds from an integer, a digit string, or an ISO 8601 time
    // with Z, a +HH:MM/-HH:MM offset, or no zone (read as UTC); unset for
    // null or "".
    std::optional<std::int64_t> stream_epoch_ms_or_iso(const json &object, const char *key);

    SportsGameResult parse_sports_result(const json &item);
    SportsGameResult parse_sports_message(const std::string &message);
} // namespace polymarket::detail
