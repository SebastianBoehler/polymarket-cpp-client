#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Request filters and lookup options for GammaClient. Unset optionals, empty
// strings and empty lists are not sent.
namespace polymarket
{
    // Filters for GammaClient::markets_pager, matching the official SDK's
    // list_markets. Dates are ISO 8601 text. `order` is a comma-separated field list;
    // "volume" and "liquidity" are sent as volumeNum and liquidityNum so they
    // sort numerically. `tag_match` is "any" or "all".
    struct GammaMarketFilter
    {
        std::optional<bool> ascending;
        std::optional<bool> closed;
        std::vector<std::string> clob_token_ids;
        std::vector<std::string> condition_ids;
        std::optional<bool> cyom;
        std::optional<bool> decimalized;
        std::string end_date_max;
        std::string end_date_min;
        std::string game_id;
        std::vector<std::int64_t> ids;
        std::optional<bool> include_tag;
        std::optional<double> liquidity_num_max;
        std::optional<double> liquidity_num_min;
        std::string locale;
        std::vector<std::string> market_maker_addresses;
        std::string order;
        std::vector<std::string> position_ids;
        std::vector<std::string> question_ids;
        std::optional<bool> related_tags;
        std::optional<bool> rfq_enabled;
        std::optional<double> rewards_min_size;
        std::vector<std::string> slugs;
        std::vector<std::string> sports_market_types;
        std::string start_date_max;
        std::string start_date_min;
        std::optional<std::int64_t> tag_id;
        std::string tag_match;
        std::string uma_resolution_status;
        std::optional<double> volume_num_max;
        std::optional<double> volume_num_min;
    };

    // Filters for GammaClient::events_pager, matching the official SDK's
    // list_events, which lists open events unless `closed` is changed.
    // `event_date` is a YYYY-MM-DD date and `recurrence` is "daily",
    // "weekly" or "monthly".
    struct GammaEventFilter
    {
        std::optional<bool> ascending;
        std::optional<bool> closed = false;
        std::optional<bool> cyom;
        std::string end_date_max;
        std::string end_date_min;
        std::optional<bool> ended;
        std::string event_date;
        std::optional<std::int64_t> event_week;
        std::vector<std::int64_t> exclude_tag_ids;
        std::optional<bool> featured;
        std::optional<bool> featured_order;
        std::vector<std::int64_t> game_ids;
        std::vector<std::int64_t> ids;
        std::optional<bool> include_best_lines;
        std::optional<bool> include_chat;
        std::optional<bool> include_children;
        std::optional<bool> include_template;
        std::optional<double> liquidity_max;
        std::optional<double> liquidity_min;
        std::optional<bool> live;
        std::string locale;
        std::string order;
        std::optional<std::int64_t> parent_event_id;
        std::string partner_slug;
        std::string recurrence;
        std::optional<bool> related_tags;
        std::vector<std::int64_t> series_ids;
        std::vector<std::string> slugs;
        std::string start_date_max;
        std::string start_date_min;
        std::string start_time_max;
        std::string start_time_min;
        std::vector<std::int64_t> tag_ids;
        std::string tag_match;
        std::string tag_slug;
        std::string title_search;
        std::optional<double> volume_max;
        std::optional<double> volume_min;
    };

    struct GammaMarketLookupOptions
    {
        std::optional<bool> include_tag;
        std::string locale;
    };

    struct GammaEventLookupOptions
    {
        std::optional<bool> include_best_lines;
        std::optional<bool> include_chat;
        std::optional<bool> include_template;
        std::string locale;
    };

    struct GammaTagLookupOptions
    {
        std::optional<bool> include_template; // tag ID lookups only
        std::string locale;
    };

    // `status` filters by event status; both fields are for tag ID lookups only.
    struct GammaRelatedTagOptions
    {
        std::optional<bool> omit_empty;
        std::string status;
    };

    struct GammaRelatedTagResourceOptions
    {
        std::string locale;
        std::optional<bool> omit_empty;
        std::string status;
    };

    // Filters for GammaClient::tags_pager, matching the official SDK's list_tags.
    struct GammaTagFilter
    {
        std::optional<bool> ascending;
        std::optional<bool> include_template;
        std::optional<bool> is_carousel;
        std::string locale;
        std::string order;
    };

    // Filters for GammaClient::series_pager, matching the official SDK's
    // list_series. `recurrence` is "daily", "weekly" or "monthly".
    struct GammaSeriesFilter
    {
        std::optional<bool> ascending;
        std::optional<bool> closed;
        std::optional<bool> exclude_events;
        std::string locale;
        std::string order;
        std::string recurrence;
        std::vector<std::string> slugs;
    };

    // Filters for GammaClient::teams_pager, matching the official SDK's list_teams.
    struct GammaTeamFilter
    {
        std::vector<std::string> abbreviations;
        std::optional<bool> ascending;
        std::vector<std::string> leagues;
        std::vector<std::string> names;
        std::string order;
        std::vector<std::int64_t> provider_ids;
    };

    // Options for GammaClient::search_pager, matching the official SDK's
    // search. `keep_closed_markets` keeps markets closed within that many
    // hours. `recurrence` is "daily", "weekly" or "monthly"; `sort` is one of
    // volume, volume_24hr, liquidity, competitive, closed_time, start_date or
    // end_date.
    struct GammaSearchOptions
    {
        std::optional<bool> ascending;
        std::optional<bool> cache;
        std::string events_status;
        std::vector<std::string> events_tags;
        std::vector<std::int64_t> exclude_tag_ids;
        std::optional<std::int64_t> keep_closed_markets;
        std::optional<bool> optimized;
        std::vector<std::string> presets;
        std::string recurrence;
        std::optional<bool> search_profiles;
        std::optional<bool> search_tags;
        std::string sort;
    };

    // Filters for GammaClient::comments_pager, matching the official SDK's
    // list_comments. Without `order`, comments are newest first and
    // `ascending` is ignored; with "id" or "createdAt" they are ascending
    // unless `ascending` is false.
    struct GammaCommentFilter
    {
        std::optional<bool> ascending;
        std::optional<bool> get_positions;
        std::optional<bool> holders_only;
        std::string order;
    };

    struct GammaUserCommentFilter
    {
        std::optional<bool> ascending;
        std::string order;
    };

} // namespace polymarket
