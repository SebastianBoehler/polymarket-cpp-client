#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// Data API (/v2) market analytics types, following the official py-sdk models.
// Units and the empty and unset rules are those of data_types.hpp, and `raw`
// holds the whole object as received.
namespace polymarket
{
    // A holder of one outcome. The PnL fields are set only when holders are
    // requested with include_pnl.
    struct DataHolder
    {
        std::string wallet;   // proxy_wallet
        std::string asset_id; // CLOB token ID
        double amount{0.0};   // shares
        std::optional<int> outcome_index;
        bool display_username_public{false};
        bool verified{false};
        std::string name;
        std::string pseudonym;
        std::string bio;
        std::string profile_image;
        std::string profile_image_optimized;
        std::optional<double> avg_price;
        std::optional<double> entry_cost_usdc;
        std::optional<double> current_price;
        std::optional<double> current_value;
        std::optional<double> realized_pnl;
        std::optional<double> unrealized_pnl;
        std::optional<double> total_pnl;
        nlohmann::json raw;
    };

    // Holders of one outcome asset. A page holds up to the page size per
    // outcome, so one asset can span pages; merge groups by `asset_id`.
    struct DataAssetHolders
    {
        std::string asset_id;
        std::vector<DataHolder> holders;
    };

    // Open interest in USDC; an empty `condition_id` is global interest.
    struct DataOpenInterest
    {
        std::string condition_id;
        double value{0.0};
        nlohmann::json raw;
    };

    struct DataMarketLiveVolume
    {
        std::string condition_id; // may be empty
        double taker_volume{0.0}; // shares
    };

    struct DataLiveVolume
    {
        double taker_volume_total{0.0}; // shares
        std::vector<DataMarketLiveVolume> markets;
        nlohmann::json raw;
    };

    // A price in USDC per share; a resolution of 0 is an exact tick.
    struct DataPricePoint
    {
        std::int64_t timestamp{0};
        double price{0.0};
        std::int64_t resolution_seconds{0};
    };

    // A market resolution. Times are Unix seconds. `status` is initialized,
    // posed, proposed, challenged, reproposed, disputed, active, arbitration
    // or resolved; `settlement_time_basis` is managed_proposal_expiration,
    // proposal_expiration, liveness or dvm_round_estimate; `market_type` is
    // BINARY, INCREMENTAL_NEGRISK or ATOMIC_NEGRISK; `resolution_source` is
    // reported or derived; `reporter` is UMA_OO, CHAINLINK or EOA. Oracle
    // prices are as reported, with the oracle's "no price" (69) unset, and
    // `payouts` are USDC per share for outcomes 0 and 1.
    struct DataResolution
    {
        std::string question_id;
        std::string condition_id;
        std::string status;
        bool extended_review{false};
        // Earliest estimated settlement time, not a deadline; it may have passed.
        std::optional<std::int64_t> expected_settlement_time;
        std::string settlement_time_basis;
        bool was_disputed{false};
        bool question_rules_updated{false}; // new_version_q
        std::optional<double> proposed_price;
        std::optional<double> reproposed_price;
        std::optional<double> price;
        std::string transaction_hash;
        std::optional<std::int64_t> log_index;
        std::int64_t last_updated_at{0};
        std::string market_type;
        std::optional<std::pair<double, double>> payouts;
        std::string resolution_source;
        std::string reporter;
        std::optional<bool> was_arbitrated;
        std::optional<std::int64_t> resolved_block;
        std::optional<std::int64_t> resolved_at;
        nlohmann::json raw;
    };

} // namespace polymarket
