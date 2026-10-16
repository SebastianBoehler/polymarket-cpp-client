#include "polymarket/gamma_client.hpp"

#include "gamma_client_internal.hpp"
#include "gamma_parsing.hpp"
#include "gamma_query.hpp"

#include <stdexcept>
#include <string>
#include <utility>

namespace polymarket
{
    Result<GammaTag> GammaClient::get_tag_by_id(const std::string &id,
                                                const GammaTagLookupOptions &options)
    {
        return fetch<GammaTag>(
            [&]
            { return detail::gamma_id_path("tag", id) + detail::gamma_tag_lookup_query(options); },
            detail::parse_gamma_tag_response);
    }

    Result<GammaTag> GammaClient::get_tag_by_slug(const std::string &slug,
                                                  const GammaTagLookupOptions &options)
    {
        return fetch<GammaTag>(
            [&]
            {
                if (options.include_template)
                    throw std::invalid_argument(
                        "include_template is only supported for tag id lookup.");
                return detail::gamma_slug_path("tag", slug) +
                       detail::gamma_tag_lookup_query(options);
            },
            detail::parse_gamma_tag_response);
    }

    Result<std::vector<GammaRelatedTag>>
    GammaClient::get_related_tags_by_id(const std::string &id,
                                        const GammaRelatedTagOptions &options)
    {
        return fetch<std::vector<GammaRelatedTag>>(
            [&]
            {
                return detail::gamma_id_path("tag", id) + "/related-tags" +
                       detail::gamma_related_tag_query(options);
            },
            detail::parse_gamma_related_tags);
    }

    Result<std::vector<GammaRelatedTag>>
    GammaClient::get_related_tags_by_slug(const std::string &slug,
                                          const GammaRelatedTagOptions &options)
    {
        return fetch<std::vector<GammaRelatedTag>>(
            [&]
            {
                if (options.omit_empty || !options.status.empty())
                    throw std::invalid_argument(
                        "omit_empty and status are only supported for related tag id lookup.");
                return detail::gamma_slug_path("tag", slug) + "/related-tags";
            },
            detail::parse_gamma_related_tags);
    }

    Result<std::vector<GammaTag>>
    GammaClient::get_related_tag_resources_by_id(const std::string &id,
                                                 const GammaRelatedTagResourceOptions &options)
    {
        return fetch<std::vector<GammaTag>>(
            [&]
            {
                return detail::gamma_id_path("tag", id) + "/related-tags/tags" +
                       detail::gamma_related_tag_resource_query(options);
            },
            detail::parse_gamma_tag_references);
    }

    Result<std::vector<GammaTag>>
    GammaClient::get_related_tag_resources_by_slug(const std::string &slug,
                                                   const GammaRelatedTagResourceOptions &options)
    {
        return fetch<std::vector<GammaTag>>(
            [&]
            {
                return detail::gamma_slug_path("tag", slug) + "/related-tags/tags" +
                       detail::gamma_related_tag_resource_query(options);
            },
            detail::parse_gamma_tag_references);
    }

    Result<GammaSeries> GammaClient::get_series(const std::string &id, const std::string &locale)
    {
        return fetch<GammaSeries>(
            [&]
            {
                if (id.empty()) throw std::invalid_argument("id cannot be empty");
                detail::GammaQuery query;
                query.add("locale", locale);
                return "/series/" + detail::percent_encode_query_value(id) + query.str();
            },
            detail::parse_gamma_series_response);
    }

    Result<std::vector<GammaSport>> GammaClient::get_sports()
    {
        return fetch<std::vector<GammaSport>>([] { return std::string("/sports"); },
                                              detail::parse_gamma_sports);
    }

    Result<std::vector<std::string>> GammaClient::get_sports_market_types()
    {
        return fetch<std::vector<std::string>>([] { return std::string("/sports/market-types"); },
                                               detail::parse_gamma_sports_market_types);
    }

    template <typename T, typename BuildQuery, typename ParseItems>
    Paginator<T> GammaClient::offset_pager(const char *endpoint, std::size_t page_size,
                                           std::size_t max_page_size, const PageRequest &resume,
                                           BuildQuery build_query, ParseItems parse_items)
    {
        std::string query;
        std::optional<SdkError> invalid;
        try
        {
            query = build_query();
        }
        catch (const std::invalid_argument &ex)
        {
            invalid = detail::gamma_invalid_argument(ex.what(), endpoint);
        }
        return detail::gamma_offset_pager<T>(
            endpoint, query, page_size, max_page_size, resume, std::move(invalid),
            [this](const std::string &path) { return get(path); }, parse_items);
    }

    Paginator<GammaTag> GammaClient::tags_pager(const GammaTagFilter &filter, std::size_t page_size,
                                                const PageRequest &resume)
    {
        return offset_pager<GammaTag>(
            "/tags", page_size, 100, resume, [&] { return detail::gamma_tag_list_query(filter); },
            detail::parse_gamma_tag_references);
    }

    Paginator<GammaSeries> GammaClient::series_pager(const GammaSeriesFilter &filter,
                                                     std::size_t page_size,
                                                     const PageRequest &resume)
    {
        return offset_pager<GammaSeries>(
            "/series", page_size, 50, resume, [&]
            { return detail::gamma_series_list_query(filter); }, detail::parse_gamma_series_list);
    }

    Paginator<GammaTeam> GammaClient::teams_pager(const GammaTeamFilter &filter,
                                                  std::size_t page_size, const PageRequest &resume)
    {
        return offset_pager<GammaTeam>(
            "/teams", page_size, 100, resume, [&] { return detail::gamma_team_list_query(filter); },
            detail::parse_gamma_teams);
    }
} // namespace polymarket
