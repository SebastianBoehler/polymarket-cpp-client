#pragma once

#include "json_fields.hpp"
#include "polymarket/sports_stream.hpp"

#include <string>

// Sports WebSocket parsing, following py-sdk models/sports_events.py.
// Malformed input throws std::exception.
namespace polymarket::detail
{
    SportsGameResult parse_sports_result(const json &item);
    SportsGameResult parse_sports_message(const std::string &message);
} // namespace polymarket::detail
