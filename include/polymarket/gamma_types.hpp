#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace polymarket
{
    // Gamma API types. Fields follow the official py-sdk models; an absent or
    // empty Gamma value leaves a string empty and an optional unset. IDs are
    // strings even where Gamma sends numbers. Dates stay as the ISO 8601 text
    // Gamma returns. Volumes, liquidity and prices are display data, so they
    // are doubles; tick size and order size keep Gamma's decimal text. `raw`
    // holds the whole object as received, for fields not modeled here.

    struct GammaTagReference
    {
        std::string id;
        std::string slug;
        std::string label;
        std::optional<bool> force_show;
        std::optional<bool> force_hide;
        std::optional<bool> is_carousel;
        std::optional<bool> requires_translation;
        std::optional<std::int64_t> created_by;
        std::optional<std::int64_t> updated_by;
        std::optional<std::int64_t> active_events_count;
        std::string published_at;
        std::string created_at;
        std::string updated_at;
        nlohmann::json raw;
    };

    struct GammaEventReference
    {
        std::string id;
        std::string slug;
        std::string title;
    };

    struct GammaSeriesReference
    {
        std::string id;
        std::string slug;
        std::string title;
    };

    struct GammaOutcome
    {
        std::string label;
        std::string token_id;    // CLOB token ID (clobTokenIds)
        std::string position_id; // Protocol V2 position ID (positionIds)
        std::optional<double> price;
    };

    struct GammaMarketState
    {
        std::optional<bool> active;
        std::optional<bool> closed;
        std::optional<bool> archived;
        std::optional<bool> accepting_orders;
        std::optional<bool> enable_order_book;
        std::optional<bool> neg_risk;
        std::string combo_status;
        std::string start_date;
        std::string end_date;
        std::string closed_time;
    };

    struct GammaMarketMetrics
    {
        std::optional<double> volume;
        std::optional<double> volume_num;
        std::optional<double> volume_24hr;
        std::optional<double> volume_1wk;
        std::optional<double> volume_1mo;
        std::optional<double> volume_1yr;
        std::optional<double> volume_amm;
        std::optional<double> volume_clob;
        std::optional<double> liquidity;
        std::optional<double> liquidity_num;
        std::optional<double> liquidity_clob;
    };

    struct GammaMarketPrices
    {
        std::optional<double> best_bid;
        std::optional<double> best_ask;
        std::optional<double> last_trade_price;
        std::optional<double> spread;
        std::optional<double> one_hour_price_change;
        std::optional<double> one_day_price_change;
        std::optional<double> one_week_price_change;
        std::optional<double> one_month_price_change;
        std::optional<double> one_year_price_change;
    };

    struct GammaMarketTrading
    {
        std::string minimum_order_size; // orderMinSize
        std::string minimum_tick_size;  // orderPriceMinTickSize
        std::optional<std::int64_t> seconds_delay;
        std::optional<bool> fees_enabled;
        std::string fee_type;
    };

    struct GammaMarketResolution
    {
        std::string question_id;
        std::string neg_risk_request_id;
        std::string uma_resolution_status;
        std::string source;
        std::string resolved_by;
    };

    struct GammaMarketRewards
    {
        std::optional<double> rewards_min_size;
        std::optional<double> rewards_max_spread;
        std::optional<bool> holding_rewards_enabled;
    };

    struct GammaMarketSports
    {
        std::string sports_market_type;
        std::optional<double> line;
        std::string game_id;
        std::string game_start_time;
    };

    // A binary market. Lists skip markets that do not have exactly two
    // outcomes, as the official SDKs do; a single lookup of one fails.
    struct GammaMarket
    {
        std::string id;
        std::string version; // "v1" or "v2" when present
        std::string slug;
        std::string condition_id;
        std::string question;
        std::string group_item_title;
        std::string description;
        std::string category;
        std::string image;
        std::string icon;
        GammaMarketState state;
        GammaOutcome yes;
        GammaOutcome no;
        GammaMarketMetrics metrics;
        GammaMarketPrices prices;
        GammaMarketTrading trading;
        GammaMarketResolution resolution;
        GammaMarketRewards rewards;
        GammaMarketSports sports;
        std::vector<GammaEventReference> events;
        std::vector<GammaTagReference> tags;
        nlohmann::json raw;
    };

    struct GammaEventState
    {
        std::optional<bool> active;
        std::optional<bool> closed;
        std::optional<bool> archived;
        std::optional<bool> is_new; // "new"
        std::optional<bool> featured;
        std::optional<bool> restricted;
        std::optional<bool> cyom;
        std::optional<bool> live;
        std::optional<bool> ended;
        std::optional<bool> comments_enabled;
    };

    struct GammaEventSchedule
    {
        std::string start_date;
        std::string creation_date;
        std::string end_date;
        std::string closed_time;
        std::string start_time;
        std::string event_date;
    };

    struct GammaEventMetrics
    {
        std::optional<double> liquidity;
        std::optional<double> liquidity_clob;
        std::optional<double> volume;
        std::optional<double> volume_24hr;
        std::optional<double> volume_1wk;
        std::optional<double> volume_1mo;
        std::optional<double> volume_1yr;
        std::optional<double> open_interest;
        std::optional<double> competitive;
        std::optional<std::int64_t> comment_count;
    };

    struct GammaEventTrading
    {
        std::optional<bool> enable_order_book;
        std::optional<bool> neg_risk;
        std::string neg_risk_market_id;
        std::optional<bool> enable_neg_risk;
        std::optional<bool> neg_risk_augmented;
        std::optional<bool> cumulative_markets;
    };

    struct GammaEvent
    {
        std::string id;
        std::string parent_event_id;
        std::string ticker;
        std::string slug;
        std::string title;
        std::string subtitle;
        std::string description;
        std::string category;
        std::string subcategory;
        std::string image;
        std::string icon;
        std::string resolution_source;
        GammaEventState state;
        GammaEventSchedule schedule;
        GammaEventMetrics metrics;
        GammaEventTrading trading;
        std::vector<GammaMarket> markets; // binary markets only
        std::vector<GammaSeriesReference> series;
        std::vector<GammaTagReference> tags;
        nlohmann::json raw;
    };

} // namespace polymarket
