#include "../src/clob_client_test_fixture.hpp"
#include "gamma_catalog_fixtures.hpp"
#include "gamma_parsing.hpp"
#include "polymarket/gamma_client.hpp"

#include <functional>
#include <string>
#include <vector>

namespace clob_test
{
    namespace
    {
        namespace fixtures = gamma_catalog_fixtures;

        Environment local_environment(const std::string &url)
        {
            auto environment = Environment::production();
            environment.gamma_url = url;
            return environment;
        }

        bool rejected(const std::function<void()> &parse)
        {
            try
            {
                parse();
            }
            catch (const std::exception &)
            {
                return true;
            }
            return false;
        }

        bool test_recorded_catalog_parsing()
        {
            const auto tag = detail::parse_gamma_tag_response(fixtures::gamma_tag);
            const auto related = detail::parse_gamma_related_tags(fixtures::gamma_related_tags);
            const auto resources =
                detail::parse_gamma_tag_references(fixtures::gamma_related_tag_resources);
            const auto series = detail::parse_gamma_series_response(fixtures::gamma_series);
            const auto sports = detail::parse_gamma_sports(fixtures::gamma_sports);
            const auto market_types =
                detail::parse_gamma_sports_market_types(fixtures::gamma_sports_market_types);
            const auto teams = detail::parse_gamma_teams(fixtures::gamma_teams);
            return check(tag.id == "21" && tag.slug == "crypto" && tag.force_show == false &&
                             tag.updated_by.has_value(),
                         "recorded tag must parse") &&
                   check(related.size() == 3 && related[0].tag_id == 21 &&
                             related[0].related_tag_id == 102175 && related[0].rank == 1,
                         "recorded related tags must parse") &&
                   check(resources.size() == 2 && resources[0].active_events_count == 345,
                         "recorded related tag resources must parse") &&
                   check(series.id == "1" && series.title == "NFL" && series.events.size() == 2 &&
                             series.events[0].id == "16407" && series.raw.contains("volume24hr"),
                         "recorded series must parse with its events and raw fields") &&
                   check(sports.size() == 2 && sports[0].id == "630" && sports[0].sport == "ufl" &&
                             sports[0].tags == "1,100639,1186,105925",
                         "recorded sports must parse") &&
                   check(market_types.size() == 5 && market_types[0] == "any_player_rampage",
                         "recorded sports market types must parse") &&
                   check(teams.size() == 3 && teams[0].id == "216158" &&
                             teams[0].provider_id == 140005569,
                         "recorded teams must parse with numeric ids as strings");
        }

        bool test_malformed_catalog_input()
        {
            return check(rejected(
                             [] { (void)detail::parse_gamma_sports(R"([{"id":1,"sport":"x"}])"); }),
                         "sports without required fields must be rejected") &&
                   check(
                       rejected(
                           []
                           { (void)detail::parse_gamma_teams(R"([{"id":1,"ordering":"left"}])"); }),
                       "a team ordering other than home or away must be rejected") &&
                   check(rejected([] { (void)detail::parse_gamma_related_tags(R"({"id":"1"})"); }),
                         "a related tags object must be rejected") &&
                   check(rejected(
                             [] { (void)detail::parse_gamma_series_response(R"({"slug":"x"})"); }),
                         "a series without id must be rejected") &&
                   check(
                       rejected([] { (void)detail::parse_gamma_sports_market_types(R"(["a"])"); }),
                       "market types must be an object");
        }

        bool test_tag_lookups()
        {
            LocalServer server;
            GammaClient client(local_environment(server.url()));
            server.enqueue(fixtures::gamma_tag);
            server.enqueue(fixtures::gamma_tag);
            server.enqueue(fixtures::gamma_related_tags);
            server.enqueue(fixtures::gamma_related_tags);
            server.enqueue(fixtures::gamma_related_tag_resources);
            server.enqueue(fixtures::gamma_related_tag_resources);
            GammaTagLookupOptions with_template;
            with_template.include_template = true;
            GammaRelatedTagOptions related_options;
            related_options.omit_empty = true;
            related_options.status = "active";
            GammaRelatedTagResourceOptions resource_options;
            resource_options.locale = "en";
            resource_options.status = "closed";
            auto by_id = client.get_tag_by_id("21", with_template);
            auto by_slug = client.get_tag_by_slug("crypto");
            auto related_id = client.get_related_tags_by_id("21", related_options);
            auto related_slug = client.get_related_tags_by_slug("crypto");
            auto resources_id = client.get_related_tag_resources_by_id("21", resource_options);
            auto resources_slug = client.get_related_tag_resources_by_slug("crypto");
            auto slug_template = client.get_tag_by_slug("crypto", with_template);
            auto slug_related = client.get_related_tags_by_slug("crypto", related_options);
            const auto requests = server.requests();
            return check(by_id.ok() && by_slug.ok() && related_id.ok() && related_slug.ok() &&
                             resources_id.ok() && resources_slug.ok(),
                         "tag lookups must parse") &&
                   check(requests.size() == 6 &&
                             requests[0].target == "/tags/21?include_template=true" &&
                             requests[1].target == "/tags/slug/crypto" &&
                             requests[2].target ==
                                 "/tags/21/related-tags?omit_empty=true&status=active" &&
                             requests[3].target == "/tags/slug/crypto/related-tags" &&
                             requests[4].target ==
                                 "/tags/21/related-tags/tags?locale=en&status=closed" &&
                             requests[5].target == "/tags/slug/crypto/related-tags/tags",
                         "tag lookups must use the official paths and parameters") &&
                   check(!slug_template.ok() &&
                             slug_template.error().code == SdkErrorCode::InvalidArgument &&
                             !slug_related.ok() &&
                             slug_related.error().code == SdkErrorCode::InvalidArgument,
                         "id-only options must be rejected on slug lookups without a request");
        }

        bool test_series_and_sports()
        {
            LocalServer server;
            GammaClient client(local_environment(server.url()));
            server.enqueue(fixtures::gamma_series);
            server.enqueue(fixtures::gamma_sports);
            server.enqueue(fixtures::gamma_sports_market_types);
            auto series = client.get_series("1", "en");
            auto sports = client.get_sports();
            auto market_types = client.get_sports_market_types();
            auto empty_series = client.get_series("");
            const auto requests = server.requests();
            return check(series.ok() && sports.ok() && sports.value().size() == 2 &&
                             market_types.ok(),
                         "series and sports lookups must parse") &&
                   check(requests.size() == 3 && requests[0].target == "/series/1?locale=en" &&
                             requests[1].target == "/sports" &&
                             requests[2].target == "/sports/market-types",
                         "series and sports lookups must use their paths") &&
                   check(!empty_series.ok() &&
                             empty_series.error().code == SdkErrorCode::InvalidArgument,
                         "an empty series id must be rejected");
        }

        bool test_offset_pagers()
        {
            LocalServer server;
            GammaClient client(local_environment(server.url()));
            server.enqueue(fixtures::gamma_tag_list);
            server.enqueue(R"([])");
            server.enqueue(fixtures::gamma_series_list);
            server.enqueue(fixtures::gamma_teams);
            GammaTagFilter tag_filter;
            tag_filter.is_carousel = false;
            tag_filter.order = "label";
            auto tags = client.tags_pager(tag_filter, 3).collect();
            GammaSeriesFilter series_filter;
            series_filter.closed = true;
            series_filter.slugs = {"nfl", "nba"};
            PageRequest resume;
            resume.offset = 40;
            auto series_pager = client.series_pager(series_filter, 50, resume);
            auto series = series_pager.collect();
            GammaTeamFilter team_filter;
            team_filter.leagues = {"ufc"};
            team_filter.provider_ids = {7};
            auto teams = client.teams_pager(team_filter, 100).collect();
            const auto requests = server.requests();
            return check(tags.ok() && tags.value().size() == 3 && series.ok() &&
                             series.value().size() == 2 && teams.ok() && teams.value().size() == 3,
                         "offset pagers must parse their pages") &&
                   check(requests.size() == 4 &&
                             requests[0].target ==
                                 "/tags?is_carousel=false&order=label&limit=3&offset=0" &&
                             requests[1].target ==
                                 "/tags?is_carousel=false&order=label&limit=3&offset=3",
                         "a full tag page must lead to the next offset") &&
                   check(requests[2].target ==
                                 "/series?closed=true&slug=nfl&slug=nba&limit=50&offset=40" &&
                             series_pager.resume_token().offset == 42,
                         "series pager must resume at the saved offset") &&
                   check(requests[3].target == "/teams?league=ufc&provider_id=7&limit=100&offset=0",
                         "teams pager must encode its filters");
        }

        bool test_invalid_offset_pagers()
        {
            LocalServer server;
            GammaClient client(local_environment(server.url()));
            GammaSeriesFilter bad_recurrence;
            bad_recurrence.recurrence = "hourly";
            auto too_many_tags = client.tags_pager({}, 101).next_page();
            auto too_many_series = client.series_pager({}, 51).next_page();
            auto zero_teams = client.teams_pager({}, 0).next_page();
            auto recurrence = client.series_pager(bad_recurrence).next_page();
            return check(!too_many_tags.ok() &&
                             too_many_tags.error().code == SdkErrorCode::InvalidArgument &&
                             !too_many_series.ok() && !zero_teams.ok() && !recurrence.ok() &&
                             recurrence.error().code == SdkErrorCode::InvalidArgument,
                         "page sizes past Gamma's caps and bad filters must fail with "
                         "InvalidArgument") &&
                   check(server.requests().empty(), "invalid offset pagers must not send requests");
        }
    } // namespace
} // namespace clob_test

int main()
{
    using namespace clob_test;
    const bool passed = test_recorded_catalog_parsing() && test_malformed_catalog_input() &&
                        test_tag_lookups() && test_series_and_sports() && test_offset_pagers() &&
                        test_invalid_offset_pagers();
    if (!passed) return 1;
    std::cout << "test_gamma_catalog passed\n";
    return 0;
}
