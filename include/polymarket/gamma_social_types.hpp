#pragma once

#include "polymarket/gamma_types.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Gamma comments, public profiles and search results, following the official
// py-sdk models with the conventions in gamma_types.hpp. Addresses are kept as
// Gamma sends them. A profile's `wallet` falls back to Gamma's proxyWallet
// field.
namespace polymarket
{
    struct GammaCommentPosition
    {
        std::string token_id;
        std::optional<std::int64_t> position_size;
    };

    struct GammaCommentProfile
    {
        std::string name;
        std::string pseudonym;
        std::string bio;
        std::string wallet;
        std::string base_address;
        std::string profile_image;
        std::optional<bool> display_username_public;
        std::optional<bool> is_mod;
        std::optional<bool> is_creator;
        std::vector<GammaCommentPosition> positions;
    };

    struct GammaReaction
    {
        std::string id;
        std::optional<std::int64_t> comment_id;
        std::string reaction_type;
        std::string icon;
        std::string user_address;
        std::string created_at;
        std::optional<GammaCommentProfile> profile;
    };

    // Attached media are left in `raw`.
    struct GammaComment
    {
        std::string id;
        std::string body;
        std::string parent_entity_type; // "Event" or "Series"
        std::string parent_entity_id;
        std::string parent_comment_id; // empty for a top-level comment
        std::string user_address;
        std::string reply_address;
        std::string created_at;
        std::string updated_at;
        std::optional<GammaCommentProfile> profile;
        std::vector<GammaReaction> reactions;
        std::optional<std::int64_t> report_count;
        std::optional<std::int64_t> reaction_count;
        std::string trade_asset;
        nlohmann::json raw;
    };

    struct GammaPublicProfileUser
    {
        std::string id;
        std::optional<bool> community_mod;
        std::optional<bool> creator;
        std::optional<bool> mod;
    };

    struct GammaPublicProfile
    {
        std::string wallet;
        std::string name;
        std::string pseudonym;
        std::string bio;
        std::string profile_image;
        std::string x_username;
        std::optional<bool> display_username_public;
        std::optional<bool> verified_badge;
        std::string created_at;
        std::vector<GammaPublicProfileUser> users;
        nlohmann::json raw;
    };

    // A profile in search results; Gamma's other profile fields are in `raw`.
    struct GammaSearchProfile
    {
        std::string id;
        std::string wallet;
        std::string name;
        std::string pseudonym;
        std::string bio;
        std::string profile_image;
        std::string x_username;
        std::optional<bool> display_username_public;
        std::optional<bool> verified_badge;
        std::string created_at;
        nlohmann::json raw;
    };

    struct GammaSearchTag
    {
        std::string id;
        std::string label;
        std::string slug;
        std::optional<std::int64_t> event_count;
    };

    // One page of /public-search results.
    struct GammaSearchResults
    {
        std::vector<GammaEvent> events; // binary markets only
        std::vector<GammaSearchTag> tags;
        std::vector<GammaSearchProfile> profiles;
        std::optional<std::int64_t> total_results;
    };

} // namespace polymarket
