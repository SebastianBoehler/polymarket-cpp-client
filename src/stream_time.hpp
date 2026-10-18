#pragma once

#include "json_fields.hpp"

#include <cstdint>
#include <optional>

// Timestamps shared by the public stream parsers. Malformed input throws
// std::invalid_argument naming the field.
namespace polymarket::detail
{
    // Unix milliseconds from an integer, a digit string, or an ISO 8601 time
    // with Z, a +HH:MM/-HH:MM offset, or no zone (read as UTC); unset for
    // null or "".
    std::optional<std::int64_t> stream_epoch_ms_or_iso(const json &object, const char *key);
} // namespace polymarket::detail
