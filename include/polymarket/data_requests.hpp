#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Request filters for DataClient, matching the official SDK. Unset optionals,
// empty strings and empty lists are not sent. Condition IDs are 31- or 32-byte
// hex (at most 20 distinct) and event IDs positive 32-bit integers; a filter
// takes condition IDs or event IDs, not both. Times are Unix seconds between
// 1 and 9999-12-31; `full_history` requests all history and cannot be combined
// with `start` or `end`.
namespace polymarket
{
    struct DataTradeFilter
    {
        std::string user; // all wallets when empty
        std::vector<std::string> condition_ids;
        std::vector<std::int64_t> event_ids;
        std::string side; // BUY or SELL
        std::optional<bool> taker_only;
        std::string filter_type;             // CASH or TOKENS, with filter_amount
        std::optional<double> filter_amount; // finite and non-negative
        // Time filters apply only when `user` is set.
        std::optional<std::int64_t> start;
        std::optional<std::int64_t> end;
        bool full_history{false};
    };

    struct DataActivityFilter
    {
        std::vector<std::string> condition_ids;
        std::vector<std::int64_t> event_ids;
        std::vector<std::string> types; // DataActivity::type values to keep
        std::string side;               // BUY or SELL
        std::string sort_direction;     // ASC or DESC
        std::optional<std::int64_t> start;
        std::optional<std::int64_t> end;
        bool full_history{false};
    };

} // namespace polymarket
