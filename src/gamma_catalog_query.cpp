#include "gamma_query.hpp"

#include <string>

// Parameter names and order follow py-sdk _internal/actions/gamma.py.
namespace polymarket::detail
{
    std::string gamma_tag_list_query(const GammaTagFilter &filter)
    {
        GammaQuery query;
        query.add("ascending", filter.ascending);
        query.add("include_template", filter.include_template);
        query.add("is_carousel", filter.is_carousel);
        query.add("locale", filter.locale);
        query.add("order", filter.order);
        return query.str();
    }

    std::string gamma_series_list_query(const GammaSeriesFilter &filter)
    {
        gamma_check_choice("recurrence", filter.recurrence, {"daily", "weekly", "monthly"});
        GammaQuery query;
        query.add("ascending", filter.ascending);
        query.add("closed", filter.closed);
        query.add("exclude_events", filter.exclude_events);
        query.add("locale", filter.locale);
        query.add("order", filter.order);
        query.add("recurrence", filter.recurrence);
        query.add("slug", filter.slugs);
        return query.str();
    }

    std::string gamma_team_list_query(const GammaTeamFilter &filter)
    {
        GammaQuery query;
        query.add("abbreviation", filter.abbreviations);
        query.add("ascending", filter.ascending);
        query.add("league", filter.leagues);
        query.add("name", filter.names);
        query.add("order", filter.order);
        query.add("provider_id", filter.provider_ids);
        return query.str();
    }

    std::string gamma_tag_lookup_query(const GammaTagLookupOptions &options)
    {
        GammaQuery query;
        query.add("include_template", options.include_template);
        query.add("locale", options.locale);
        return query.str();
    }

    std::string gamma_related_tag_query(const GammaRelatedTagOptions &options)
    {
        GammaQuery query;
        query.add("omit_empty", options.omit_empty);
        query.add("status", options.status);
        return query.str();
    }

    std::string gamma_related_tag_resource_query(const GammaRelatedTagResourceOptions &options)
    {
        GammaQuery query;
        query.add("locale", options.locale);
        query.add("omit_empty", options.omit_empty);
        query.add("status", options.status);
        return query.str();
    }
} // namespace polymarket::detail
