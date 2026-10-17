#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Data API (/v2) portfolio types, following the official py-sdk models. Units
// and the empty and unset rules are those of data_types.hpp: amounts, costs,
// values and PnL are USDC, sizes and volumes are shares, percentages are
// percentages, and `raw` holds the whole object as received.
namespace polymarket
{
    struct DataPosition
    {
        std::string wallet;   // proxy_wallet
        std::string asset_id; // CLOB token ID
        std::string condition_id;
        double current_size{0.0};
        double avg_price{0.0};
        double entry_cost_usdc{0.0}; // fee-exclusive
        double entry_fees_usdc{0.0};
        double total_cost_usdc{0.0};
        double current_price{0.0};
        double current_value{0.0};
        double total_size{0.0};
        double realized_pnl{0.0};
        double unrealized_pnl{0.0};
        double total_pnl{0.0};
        double percent_pnl{0.0};
        double percent_realized_pnl{0.0};
        // OPEN, REDEEMABLE or CLOSED. A lost position reports REDEEMABLE and a
        // mergeable one OPEN.
        std::string status;
        bool redeemable{false};
        bool mergeable{false};
        bool negative_risk{false};
        bool archived{false};
        bool verified{false};
        std::string title;
        std::string slug;
        std::string icon;
        std::string event_slug;
        std::string event_id;
        std::string outcome;
        std::optional<int> outcome_index;
        std::string opposite_outcome;
        std::string opposite_asset_id;
        std::string end_date; // YYYY-MM-DD
        std::optional<std::int64_t> last_event_at;
        std::optional<std::int64_t> first_entry_at;
        std::string name;
        std::string profile_image;
        nlohmann::json raw;
    };

    struct DataPortfolioValue
    {
        std::string wallet;
        double value{0.0};
        nlohmann::json raw;
    };

    // Cumulative wallet metrics at one time. Amounts the Data API leaves out
    // stay unset.
    struct DataPnlPoint
    {
        std::int64_t timestamp{0};
        std::int64_t source_block{0};
        std::int64_t trade_count{0};
        double realized_market_pnl{0.0};
        double realized_lp_pnl{0.0};
        double realized_combo_pnl{0.0};
        double realized_pnl{0.0};
        double volume{0.0}; // shares
        double volume_usdc{0.0};
        std::optional<double> unrealized_pnl;
        std::optional<double> fees_refunded;
        std::optional<double> maker_rebate;
        std::optional<double> taker_rebate;
        std::optional<double> reward_income;
        std::optional<double> yield_income;
        std::optional<double> referral_income;
        std::optional<double> sponsored_income;
        std::optional<double> deposits;
        std::optional<double> withdrawals;
        std::optional<double> cashflow_net;
        std::optional<double> wallet_income;
        std::optional<double> position_pnl;
        std::optional<double> settled_pnl;
        std::optional<double> economic_pnl;
        std::optional<double> trade_pnl;
        std::optional<double> fees;
        std::optional<double> fees_paid;
        nlohmann::json raw;
    };

    struct DataUserStats
    {
        std::string wallet;
        std::int64_t traded_market_count{0}; // trades
        double biggest_win{0.0};
        std::int64_t views{0};
        std::optional<std::int64_t> join_date;
        std::optional<DataPnlPoint> all_time_pnl;
        nlohmann::json raw;
    };

    // A cumulative PnL series. `interval` is max, all, 1m, 1w, 1d, 12h or 6h;
    // `fidelity` and `source_fidelity` are 1d, 18h, 12h, 3h or 1h. Each point
    // keeps its own `raw`.
    struct DataPnlSeries
    {
        std::string wallet;
        std::string interval;
        std::string fidelity;
        std::string source_fidelity;
        std::vector<DataPnlPoint> points;
    };

    struct DataUserVolume
    {
        double volume{0.0}; // shares
        double volume_usdc{0.0};
        std::int64_t trade_count{0};
        nlohmann::json raw;
    };

} // namespace polymarket
