#pragma once

#include "polymarket/gamma_catalog_types.hpp"
#include "polymarket/gamma_social_types.hpp"
#include "polymarket/gamma_types.hpp"
#include "json_fields.hpp"
#include "polymarket/pagination.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Gamma response parsing. Malformed input throws std::exception.
namespace polymarket::detail
{
    using json = nlohmann::json;

    GammaTagReference parse_gamma_tag_reference(const json &item);
    // Nullopt for a market that does not have exactly two outcomes.
    std::optional<GammaMarket> parse_gamma_binary_market(const json &item);
    GammaEvent parse_gamma_event(const json &item);

    GammaMarket parse_gamma_market_response(const std::string &body);
    GammaEvent parse_gamma_event_response(const std::string &body);
    std::vector<GammaTagReference> parse_gamma_tag_references(const std::string &body);
    // `{items_key: [...], next_cursor?}`; a missing or null cursor ends paging.
    Page<GammaMarket> parse_gamma_market_keyset_page(const std::string &body);
    Page<GammaEvent> parse_gamma_event_keyset_page(const std::string &body);

    GammaTag parse_gamma_tag_response(const std::string &body);
    std::vector<GammaRelatedTag> parse_gamma_related_tags(const std::string &body);
    GammaSeries parse_gamma_series(const json &item);
    GammaSeries parse_gamma_series_response(const std::string &body);
    std::vector<GammaSeries> parse_gamma_series_list(const std::string &body);
    std::vector<GammaSport> parse_gamma_sports(const std::string &body);
    std::vector<std::string> parse_gamma_sports_market_types(const std::string &body);
    std::vector<GammaTeam> parse_gamma_teams(const std::string &body);

    GammaComment parse_gamma_comment(const json &item);
    GammaReaction parse_gamma_reaction(const json &item);
    std::vector<GammaComment> parse_gamma_comments(const std::string &body);
    Page<GammaComment> parse_gamma_comment_keyset_page(const std::string &body);
    // An offset page counted by its top-level comments.
    Page<GammaComment> parse_gamma_comment_root_page(const std::string &body);
    GammaPublicProfile parse_gamma_public_profile(const std::string &body);
    // One aggregate result; `has_more` from pagination.hasMore.
    Page<GammaSearchResults> parse_gamma_search_page(const std::string &body);
} // namespace polymarket::detail
