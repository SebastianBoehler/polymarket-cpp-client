#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>

// Data API (/v2) types, following the official py-sdk models. Amounts are USDC,
// sizes and shares are outcome units and prices are USDC per share; they are
// reporting values, so they are doubles. Timestamps are Unix seconds. An absent
// or empty Data API value leaves a string empty and an optional unset, and an
// outcome index of 999 (Data API's "none") reads as unset. `raw` holds the whole
// object as received.
namespace polymarket
{
    struct DataTrade
    {
        std::string wallet;   // proxy_wallet
        std::string asset_id; // CLOB token ID
        std::string condition_id;
        std::string side; // BUY or SELL
        double size{0.0};
        double price{0.0};
        std::int64_t timestamp{0};
        std::string transaction_hash;
        std::string title;
        std::string slug;
        std::string icon;
        std::string event_slug;
        std::string outcome;
        std::optional<int> outcome_index;
        std::string name;
        std::string pseudonym;
        std::string bio;
        std::string profile_image;
        std::string profile_image_optimized;
        nlohmann::json raw;
    };

    // One wallet activity row. `type` is TRADE, SPLIT, MERGE, REDEEM,
    // CONVERSION, REWARD, DEPOSIT, WITHDRAWAL, MAKER_REBATE, TAKER_REBATE,
    // REFERRAL_REWARD, MIGRATION, TIP, YIELD, or a newer kind kept as received
    // (then only the wallet, time, hash and profile fields are read; the rest
    // is in `raw`). Fields a kind does not carry stay empty:
    // - TRADE: condition_id, asset_id, side, shares, amount, price, outcome,
    //   outcome_index and market text. A combo trade (is_combo) has a combo
    //   condition_id, a position ID in asset_id, and only title and icon text.
    // - SPLIT, MERGE, REDEEM, CONVERSION: condition_id, amount and market text.
    // - Account credits (REWARD through YIELD): amount; TIP also tip_side.
    struct DataActivity
    {
        std::string type;
        bool known{false}; // one of the kinds listed above
        bool is_combo{false};
        std::string wallet;
        std::optional<std::int64_t> timestamp; // always set for a known kind
        std::string transaction_hash;
        std::string condition_id;
        std::string asset_id;
        std::string side;             // BUY or SELL for a trade
        std::optional<double> shares; // size
        std::optional<double> amount; // usdc_size
        std::optional<double> price;
        std::string outcome;
        std::optional<int> outcome_index;
        std::string title;
        std::string slug;
        std::string icon;
        std::string event_slug;
        std::string tip_side; // IN or OUT for a tip
        std::string name;
        std::string pseudonym;
        std::string bio;
        std::string profile_image;
        std::string profile_image_optimized;
        nlohmann::json raw;
    };

} // namespace polymarket
