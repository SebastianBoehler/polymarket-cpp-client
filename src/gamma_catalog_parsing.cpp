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
            tag.id = field_required_id(item, "id");
            tag.tag_id = field_integer(item, "tagID");
            tag.related_tag_id = field_integer(item, "relatedTagID");
            tag.rank = field_integer(item, "rank");
            tag.raw = item;
            return tag;
        }

        GammaSport parse_sport(const json &item)
        {
            GammaSport sport;
            sport.id = field_required_id(item, "id");
            sport.sport = field_required_string(item, "sport");
            sport.name = field_string(item, "name");
            sport.image = field_required_string(item, "image");
            sport.resolution = field_required_string(item, "resolution");
            sport.ordering = field_required_string(item, "ordering");
            sport.tags = field_required_string(item, "tags");
            sport.series = field_required_string(item, "series");
            sport.created_at = field_string(item, "createdAt");
            sport.updated_at = field_string(item, "updatedAt");
            sport.raw = item;
            return sport;
        }

        GammaTeam parse_team(const json &item)
        {
            GammaTeam team;
            team.id = field_required_id(item, "id");
            team.name = field_string(item, "name");
            team.league = field_string(item, "league");
            team.record = field_string(item, "record");
            team.logo = field_string(item, "logo");
            team.abbreviation = field_string(item, "abbreviation");
            team.alias = field_string(item, "alias");
            team.color = field_string(item, "color");
            team.ordering = field_string(item, "ordering");
            if (!team.ordering.empty() && team.ordering != "home" && team.ordering != "away")
                throw std::invalid_argument("Gamma field ordering must be home or away");
            team.provider_id = field_integer(item, "providerId");
            team.created_at = field_string(item, "createdAt");
            team.updated_at = field_string(item, "updatedAt");
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
        series.id = field_required_id(item, "id");
        series.ticker = field_string(item, "ticker");
        series.slug = field_string(item, "slug");
        series.title = field_string(item, "title");
        series.subtitle = field_string(item, "subtitle");
        series.series_type = field_string(item, "seriesType");
        series.recurrence = field_string(item, "recurrence");
        series.description = field_string(item, "description");
        series.image = field_string(item, "image");
        series.icon = field_string(item, "icon");
        series.layout = field_string(item, "layout");
        series.active = field_bool(item, "active");
        series.closed = field_bool(item, "closed");
        series.archived = field_bool(item, "archived");
        series.is_new = field_bool(item, "new");
        series.featured = field_bool(item, "featured");
        series.restricted = field_bool(item, "restricted");
        series.comments_enabled = field_bool(item, "commentsEnabled");
        series.competitive = field_string(item, "competitive");
        series.volume = field_number(item, "volume");
        series.liquidity = field_number(item, "liquidity");
        series.comment_count = field_integer(item, "commentCount");
        series.start_date = field_string(item, "startDate");
        series.published_at = field_string(item, "publishedAt");
        series.created_at = field_string(item, "createdAt");
        series.updated_at = field_string(item, "updatedAt");
        for (const auto &event : field_objects(item, "events"))
            series.events.push_back(parse_gamma_event(event));
        for (const auto &tag : field_objects(item, "tags"))
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
        return field_string_list(json::parse(body), "marketTypes");
    }

    std::vector<GammaTeam> parse_gamma_teams(const std::string &body)
    {
        return parse_array(body, "teams", parse_team);
    }
} // namespace polymarket::detail
