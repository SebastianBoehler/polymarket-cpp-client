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

    // Positions need `user`, or exactly one condition ID without one. Condition
    // IDs here are 32-byte, or 31-byte structural market IDs starting 0x01 or
    // 0x02, which are padded with 00; all are sent lowercased. Positions are
    // unbounded in time by default.
    struct DataPositionFilter
    {
        std::string user;
        std::vector<std::string> condition_ids;
        std::vector<std::int64_t> event_ids; // requires `user`
        // OPEN, REDEEMABLE, CLOSED, or REDEEMABLE_LOST and MERGEABLE, which
        // require `user`.
        std::string status;
        std::string title; // at most 200 characters; blank is not sent
        std::string filter_type;             // CASH or TOKENS, with filter_amount
        std::optional<double> filter_amount; // finite and non-negative
        std::optional<bool> include_archived; // not true with CLOSED
        // CURRENT_VALUE, PRICE, TOKENS, UNREALIZED_PNL, REALIZED_PNL, TOTAL_PNL
        // or TIMESTAMP.
        std::string sort_by;
        std::string sort_direction; // ASC or DESC
        std::optional<std::int64_t> start;
        std::optional<std::int64_t> end;
    };

    struct DataTimeRange
    {
        std::optional<std::int64_t> start;
        std::optional<std::int64_t> end;
        bool full_history{false};
    };

} // namespace polymarket
