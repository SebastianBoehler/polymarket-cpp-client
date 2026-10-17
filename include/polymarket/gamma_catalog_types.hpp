#pragma once

#include "polymarket/gamma_types.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Gamma tags, series, sports and teams, following the official py-sdk models
// with the conventions in gamma_types.hpp.
namespace polymarket
{
    // Tag templates are left in `raw`.
    using GammaTag = GammaTagReference;

    struct GammaRelatedTag
    {
        std::string id;
        std::optional<std::int64_t> tag_id;
        std::optional<std::int64_t> related_tag_id;
        std::optional<std::int64_t> rank;
        nlohmann::json raw;
    };

    struct GammaSeries
    {
        std::string id;
        std::string ticker;
        std::string slug;
        std::string title;
        std::string subtitle;
        std::string series_type;
        std::string recurrence;
        std::string description;
        std::string image;
        std::string icon;
        std::string layout;
        std::optional<bool> active;
        std::optional<bool> closed;
        std::optional<bool> archived;
        std::optional<bool> is_new; // "new"
        std::optional<bool> featured;
        std::optional<bool> restricted;
        std::optional<bool> comments_enabled;
        std::string competitive; // Gamma sends this as text
        std::optional<double> volume;
        std::optional<double> liquidity;
        std::optional<std::int64_t> comment_count;
        std::string start_date;
        std::string published_at;
        std::string created_at;
        std::string updated_at;
        std::vector<GammaEvent> events;
        std::vector<GammaTag> tags;
        nlohmann::json raw;
    };

    struct GammaSport
    {
        std::string id;
        std::string sport;
        std::string name;
        std::string image;
        std::string resolution;
        std::string ordering;
        std::string tags;   // comma-separated tag IDs
        std::string series; // series ID
        std::string created_at;
        std::string updated_at;
        nlohmann::json raw;
    };

    struct GammaTeam
    {
        std::string id;
        std::string name;
        std::string league;
        std::string record;
        std::string logo;
        std::string abbreviation;
        std::string alias;
        std::string color;
        std::string ordering; // "home" or "away" when present
        std::optional<std::int64_t> provider_id;
        std::string created_at;
        std::string updated_at;
        nlohmann::json raw;
    };

} // namespace polymarket
