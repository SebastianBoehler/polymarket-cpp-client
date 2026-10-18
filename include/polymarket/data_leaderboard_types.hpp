#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>

// Data API (/v2) leaderboard types, following the official py-sdk models. PnL
// and values are USDC and volume is shares. The empty and unset rules are
// those of data_types.hpp, and `raw` holds the whole object as received.
namespace polymarket
{
    // A ranked trader. Ranks can tie and skip.
    struct DataTraderEntry
    {
        std::int64_t rank{0};
        std::string wallet; // user_id
        double pnl{0.0};
        double volume{0.0};
        std::string user_name;
        std::string profile_image;
        std::string x_username;
        bool verified{false};
        nlohmann::json raw;
    };

    // One trader's ranks; a rank is unset when the trader is unranked.
    struct DataTraderStanding
    {
        std::string wallet; // user_id
        double pnl{0.0};
        double volume{0.0};
        std::optional<std::int64_t> pnl_rank;
        std::optional<std::int64_t> volume_rank;
        std::string user_name;
        std::string profile_image;
        std::string x_username;
        bool verified{false};
        nlohmann::json raw;
    };

    // A winning position. `kind` is "market" or "combo". A market winner has
    // its condition ID, outcome asset ID and event. A combo winner has the
    // common fields, a combo condition ID in its 31-byte form, lowercased, and
    // its position ID in `asset_id`; its event fields are empty.
    struct DataBiggestWinner
    {
        std::int64_t rank{0}; // win_rank
        std::string kind;
        std::string wallet; // user_id
        double pnl{0.0};
        double initial_value{0.0};
        double final_value{0.0};
        std::int64_t resolved_at{0}; // Unix seconds
        std::string user_name;
        std::string profile_image;
        std::string event_title;
        std::string condition_id;
        std::string asset_id; // position_id
        std::string event_id;
        std::string event_slug;
        nlohmann::json raw;
    };

} // namespace polymarket
