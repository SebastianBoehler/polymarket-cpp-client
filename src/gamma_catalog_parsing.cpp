#include "gamma_parsing.hpp"

#include <stdexcept>
#include <string>
#include <utility>

// Field names follow py-sdk models/gamma/tag.py, series.py and common.py.
namespace polymarket::detail
{
    namespace
    {
        template <typename Parse>
        auto parse_array(const std::string &body, const char *what, Parse parse)
        {
            const auto parsed = json::parse(body);
            if (!parsed.is_array())
                throw std::invalid_argument(std::string("Gamma ") + what +
                                            " response must be an array");
            std::vector<decltype(parse(parsed))> items;
            items.reserve(parsed.size());
            for (const auto &item : parsed)
                items.push_back(parse(item));
            return items;
        }

        GammaRelatedTag parse_related_tag(const json &item)
        {
            GammaRelatedTag tag;
            tag.id = gamma_required_id(item, "id");
            tag.tag_id = gamma_integer(item, "tagID");
            tag.related_tag_id = gamma_integer(item, "relatedTagID");
            tag.rank = gamma_integer(item, "rank");
            tag.raw = item;
            return tag;
        }

        GammaSport parse_sport(const json &item)
        {
            GammaSport sport;
            sport.id = gamma_required_id(item, "id");
            sport.sport = gamma_required_string(item, "sport");
            sport.name = gamma_string(item, "name");
            sport.image = gamma_required_string(item, "image");
            sport.resolution = gamma_required_string(item, "resolution");
            sport.ordering = gamma_required_string(item, "ordering");
            sport.tags = gamma_required_string(item, "tags");
            sport.series = gamma_required_string(item, "series");
            sport.created_at = gamma_string(item, "createdAt");
            sport.updated_at = gamma_string(item, "updatedAt");
            sport.raw = item;
            return sport;
        }

        GammaTeam parse_team(const json &item)
        {
            GammaTeam team;
            team.id = gamma_required_id(item, "id");
            team.name = gamma_string(item, "name");
            team.league = gamma_string(item, "league");
            team.record = gamma_string(item, "record");
            team.logo = gamma_string(item, "logo");
            team.abbreviation = gamma_string(item, "abbreviation");
            team.alias = gamma_string(item, "alias");
            team.color = gamma_string(item, "color");
            team.ordering = gamma_string(item, "ordering");
            if (!team.ordering.empty() && team.ordering != "home" && team.ordering != "away")
                throw std::invalid_argument("Gamma field ordering must be home or away");
            team.provider_id = gamma_integer(item, "providerId");
            team.created_at = gamma_string(item, "createdAt");
            team.updated_at = gamma_string(item, "updatedAt");
            team.raw = item;
            return team;
        }
    } // namespace

    GammaTag parse_gamma_tag_response(const std::string &body)
    {
        return parse_gamma_tag_reference(json::parse(body));
    }

    std::vector<GammaRelatedTag> parse_gamma_related_tags(const std::string &body)
    {
        return parse_array(body, "related tags", parse_related_tag);
    }

    GammaSeries parse_gamma_series(const json &item)
    {
        GammaSeries series;
        series.id = gamma_required_id(item, "id");
        series.ticker = gamma_string(item, "ticker");
        series.slug = gamma_string(item, "slug");
        series.title = gamma_string(item, "title");
        series.subtitle = gamma_string(item, "subtitle");
        series.series_type = gamma_string(item, "seriesType");
        series.recurrence = gamma_string(item, "recurrence");
        series.description = gamma_string(item, "description");
        series.image = gamma_string(item, "image");
        series.icon = gamma_string(item, "icon");
        series.layout = gamma_string(item, "layout");
        series.active = gamma_bool(item, "active");
        series.closed = gamma_bool(item, "closed");
        series.archived = gamma_bool(item, "archived");
        series.is_new = gamma_bool(item, "new");
        series.featured = gamma_bool(item, "featured");
        series.restricted = gamma_bool(item, "restricted");
        series.comments_enabled = gamma_bool(item, "commentsEnabled");
        series.competitive = gamma_string(item, "competitive");
        series.volume = gamma_number(item, "volume");
        series.liquidity = gamma_number(item, "liquidity");
        series.comment_count = gamma_integer(item, "commentCount");
        series.start_date = gamma_string(item, "startDate");
        series.published_at = gamma_string(item, "publishedAt");
        series.created_at = gamma_string(item, "createdAt");
        series.updated_at = gamma_string(item, "updatedAt");
        for (const auto &event : gamma_objects(item, "events"))
            series.events.push_back(parse_gamma_event(event));
        for (const auto &tag : gamma_objects(item, "tags"))
            series.tags.push_back(parse_gamma_tag_reference(tag));
        series.raw = item;
        return series;
    }

    GammaSeries parse_gamma_series_response(const std::string &body)
    {
        return parse_gamma_series(json::parse(body));
    }

    std::vector<GammaSeries> parse_gamma_series_list(const std::string &body)
    {
        return parse_array(body, "series", parse_gamma_series);
    }

    std::vector<GammaSport> parse_gamma_sports(const std::string &body)
    {
        return parse_array(body, "sports", parse_sport);
    }

    std::vector<std::string> parse_gamma_sports_market_types(const std::string &body)
    {
        return gamma_string_list(json::parse(body), "marketTypes");
    }

    std::vector<GammaTeam> parse_gamma_teams(const std::string &body)
    {
        return parse_array(body, "teams", parse_team);
    }
} // namespace polymarket::detail
