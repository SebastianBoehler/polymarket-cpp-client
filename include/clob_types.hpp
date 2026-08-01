#pragma once

#include "order_signer.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace polymarket
{
    // Order types supported by Polymarket
    enum class OrderType
    {
        GTC, // Good-Til-Cancelled
        GTD, // Good-Til-Date
        FOK, // Fill-Or-Kill
        FAK  // Fill-And-Kill
    };

    // Order response from API
    struct OrderResponse
    {
        bool success{false};
        std::string error_msg;
        std::string order_id;
        std::vector<std::string> transaction_hashes;
        std::vector<std::string> trade_ids;
        std::string status;
        std::string taking_amount; // Shares received
        std::string making_amount; // USDC spent
    };

    // Open order info
    struct OpenOrder
    {
        std::string id;
        std::string owner;
        std::string maker_address;
        std::string market;
        std::string asset_id;
        std::string side;
        std::string original_size;
        std::string size_matched;
        std::string price;
        std::string status;
        std::vector<std::string> associate_trades;
        std::string outcome;
        std::string created_at;
        std::string expiration;
        std::string order_type;
    };

    struct MakerOrder
    {
        std::string order_id;
        std::string owner;
        std::string maker_address;
        std::string matched_amount;
        std::string price;
        std::string fee_rate_bps;
        std::string asset_id;
        std::string outcome;
        std::string side;
    };

    // Authenticated Trade V2 response
    struct Trade
    {
        std::string id;
        std::string taker_order_id;
        std::string market;
        std::string asset_id;
        std::string side;
        std::string size;
        std::string fee_rate_bps;
        std::string price;
        std::string status;
        std::string match_time;
        std::string last_update;
        std::string outcome;
        uint32_t bucket_index{0};
        std::string owner;
        std::string maker_address;
        std::vector<MakerOrder> maker_orders;
        std::string transaction_hash;
        std::string trader_side;
        std::optional<std::string> error_msg;
    };

    // Balance/Allowance info
    struct BalanceAllowance
    {
        std::string balance;
        std::map<std::string, std::string> allowances;
    };

    // Price info
    struct PriceInfo
    {
        std::string token_id;
        double price{0.0};
    };

    // Midpoint info
    struct MidpointInfo
    {
        std::string token_id;
        double mid{0.0};
    };

    // Spread info
    struct SpreadInfo
    {
        std::string token_id;
        double spread{0.0};
    };

    // Tick size info
    struct TickSizeInfo
    {
        std::string minimum_tick_size;
    };

    // Neg risk info
    struct NegRiskInfo
    {
        bool neg_risk{false};
    };

    // Order scoring result
    struct OrderScoringResult
    {
        bool scoring{false};
    };

    // Create order parameters
    struct CreateOrderParams
    {
        std::string token_id;
        double price{0.0};
        double size{0.0};
        OrderSide side{OrderSide::BUY};
        std::string tick_size;
        std::string expiration = "0";
        std::string metadata = "0x0000000000000000000000000000000000000000000000000000000000000000";
        std::string builder_code = "0x0000000000000000000000000000000000000000000000000000000000000000";
        std::optional<bool> neg_risk; // If set, skips API call to fetch neg_risk
    };

    // Create market order parameters
    struct CreateMarketOrderParams
    {
        std::string token_id;
        double amount{0.0}; // USDC for BUY, shares for SELL
        OrderSide side{OrderSide::BUY};
        std::optional<double> price; // Optional price limit
        std::string tick_size;
        std::string metadata = "0x0000000000000000000000000000000000000000000000000000000000000000";
        std::string builder_code = "0x0000000000000000000000000000000000000000000000000000000000000000";
        std::optional<bool> neg_risk; // If set, skips API call to fetch neg_risk
    };

    // Signed order paired with the execution policy used when posting it.
    struct PreparedOrder
    {
        PreparedOrder() = delete;
        PreparedOrder(SignedOrder signed_order, OrderType type)
            : order(std::move(signed_order)), order_type(type) {}

        SignedOrder order;
        OrderType order_type;
    };

    using BatchOrderEntry = PreparedOrder;

    struct PriceHistoryPoint
    {
        uint64_t timestamp{0};
        double price{0.0};
    };

    struct LiveActivityMarket
    {
        std::string condition_id;
        int64_t id = 0;
        std::string question;
        std::string market_slug;
        std::string event_slug;
        std::string series_slug;
        std::string icon;
        std::string image;
        std::vector<std::string> tags;
    };

    struct Notification
    {
        uint32_t type{0};
        std::string owner;
        nlohmann::json payload;
    };

    struct RewardToken
    {
        std::string token_id;
        std::string outcome;
        std::string price;
    };

    struct RewardsConfig
    {
        std::string id;
        std::string asset_address;
        std::string start_date;
        std::string end_date;
        std::string rate_per_day;
        std::string total_rewards;
        std::string total_days;
    };

    struct RewardsInfo
    {
        std::string condition_id;
        std::string question;
        std::string market_slug;
        std::string event_slug;
        std::string image;
        std::string rewards_max_spread;
        std::string rewards_min_size;
        std::string market_competitiveness;
        std::vector<RewardToken> tokens;
        std::vector<RewardsConfig> rewards_config;
    };

    struct EarningsInfo
    {
        std::string date;
        std::string condition_id;
        std::string asset_address;
        std::string maker_address;
        std::string earnings;
        std::string asset_rate;
    };

    struct FeeRateInfo
    {
        std::string maker;
        std::string taker;
        std::string base_fee;
    };

    struct Position
    {
        std::string proxy_wallet;
        std::string asset; // Token ID
        std::string condition_id;
        double size{0.0}; // Number of shares
        double avg_price{0.0};
        double initial_value{0.0};
        double current_value{0.0};
        double cash_pnl{0.0};
        double percent_pnl{0.0};
        double total_bought{0.0};
        double realized_pnl{0.0};
        double percent_realized_pnl{0.0};
        double cur_price{0.0};
        bool redeemable{false};
        bool mergeable{false};
        std::string title;
        std::string slug;
        std::string icon;
        std::string event_slug;
        std::string outcome;
        int outcome_index{0};       // 0 or 1
        std::string opposite_outcome;
        std::string opposite_asset; // Token ID of opposite outcome
        std::string end_date;
        bool negative_risk{false};
    };
}
