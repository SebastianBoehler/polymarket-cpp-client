#pragma once

#include "polymarket/gamma_client.hpp"
#include "query_builder.hpp"

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Gamma request paths and query strings. Invalid input throws
// std::invalid_argument.
namespace polymarket::detail
{
    std::string gamma_market_list_query(const GammaMarketFilter &filter);
    std::string gamma_event_list_query(const GammaEventFilter &filter);
    std::string gamma_market_lookup_query(const GammaMarketLookupOptions &options);
    std::string gamma_event_lookup_query(const GammaEventLookupOptions &options);

    std::string gamma_tag_list_query(const GammaTagFilter &filter);
    std::string gamma_series_list_query(const GammaSeriesFilter &filter);
    std::string gamma_team_list_query(const GammaTeamFilter &filter);
    std::string gamma_tag_lookup_query(const GammaTagLookupOptions &options);
    std::string gamma_related_tag_query(const GammaRelatedTagOptions &options);
    std::string gamma_related_tag_resource_query(const GammaRelatedTagResourceOptions &options);

    // "/<kind>s/<id>" or "/<kind>s/slug/<slug>", each segment percent-encoded.
    std::string gamma_id_path(std::string_view kind, const std::string &id);
    std::string gamma_slug_path(std::string_view kind, const std::string &slug);
    // Slug from a https://polymarket.com URL, as py-sdk gamma_paths.py reads it.
    std::string gamma_slug_from_url(std::string_view kind, const std::string &url);
} // namespace polymarket::detail
