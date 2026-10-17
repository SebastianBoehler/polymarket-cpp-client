#include "../src/clob_client_test_fixture.hpp"
#include "gamma_fixtures.hpp"
#include "polymarket/gamma_client.hpp"

#include <string>
#include <vector>

namespace clob_test
{
    namespace
    {
        Environment local_environment(const std::string &url)
        {
            auto environment = Environment::production();
            environment.gamma_url = url;
            return environment;
        }

        bool test_market_lookups()
        {
            LocalServer server;
            GammaClient client(local_environment(server.url()));
            for (int i = 0; i < 5; ++i)
                server.enqueue(gamma_fixtures::gamma_market);
            GammaMarketLookupOptions options;
            options.include_tag = true;
            options.locale = "en";
            auto by_slug = client.get_market_by_slug("xi-jinping-out-before-2027", options);
            auto by_id = client.get_market_by_id("559651");
            auto by_event_url =
                client.get_market_by_url("https://polymarket.com/event/xi/market+slug?tid=1");
            auto by_market_url =
                client.get_market_by_url("https://www.polymarket.com/market/xi-market");
            auto by_event_only =
                client.get_market_by_url("https://polymarket.com/event/xi-event#top");
            const auto requests = server.requests();
            return check(by_slug.ok() && by_slug.value().id == "559651" && by_id.ok(),
                         "market lookups must parse") &&
                   check(requests.size() == 5 &&
                             requests[0].target ==
                                 "/markets/slug/"
                                 "xi-jinping-out-before-2027?include_tag=true&locale=en" &&
                             requests[1].target == "/markets/559651",
                         "market lookups must use the slug and id paths") &&
                   check(requests[2].target == "/markets/slug/market%2Bslug" &&
                             requests[3].target == "/markets/slug/xi-market" &&
                             requests[4].target == "/markets/slug/xi-event",
                         "market URLs must resolve like the official SDK") &&
                   check(by_event_url.ok() && by_market_url.ok() && by_event_only.ok(),
                         "URL lookups must parse");
        }

        bool test_event_lookups_and_tags()
        {
            LocalServer server;
            GammaClient client(local_environment(server.url()));
            server.enqueue(gamma_fixtures::gamma_event);
            server.enqueue(gamma_fixtures::gamma_event);
            server.enqueue(gamma_fixtures::gamma_tags);
            server.enqueue(gamma_fixtures::gamma_tags);
            GammaEventLookupOptions options;
            options.include_chat = false;
            auto by_url = client.get_event_by_url(
                "https://polymarket.com/event/kraken-ipo-in-2025?tid=1", options);
            auto by_id = client.get_event_by_id("16183");
            auto market_tags = client.get_market_tags("559651");
            auto event_tags = client.get_event_tags("16183");
            const auto requests = server.requests();
            return check(by_url.ok() && by_url.value().markets.size() == 2 && by_id.ok(),
                         "event lookups must parse") &&
                   check(market_tags.ok() && market_tags.value().size() == 2 && event_tags.ok(),
                         "tags must parse") &&
                   check(requests.size() == 4 &&
                             requests[0].target ==
                                 "/events/slug/kraken-ipo-in-2025?include_chat=false" &&
                             requests[1].target == "/events/16183" &&
                             requests[2].target == "/markets/559651/tags" &&
                             requests[3].target == "/events/16183/tags",
                         "event and tag lookups must use their paths");
        }

        bool test_lookup_failures()
        {
            LocalServer server;
            GammaClient client(local_environment(server.url()));
            server.enqueue(R"({"type":"not found error","error":"slug not found"})", 404);
            server.enqueue(R"({"id":"1","outcomes":["A","B","C"]})");
            auto missing = client.get_market_by_slug("no-such-market");
            auto non_binary = client.get_market_by_id("1");
            auto empty_id = client.get_event_by_id("");
            auto wrong_host = client.get_event_by_url("https://polymarket.example/event/x");
            auto wrong_scheme = client.get_market_by_url("http://polymarket.com/market/x");
            auto wrong_kind = client.get_event_by_url("https://polymarket.com/market/x");
            const bool rejected_locally =
                !empty_id.ok() && empty_id.error().code == SdkErrorCode::InvalidArgument &&
                !wrong_host.ok() && wrong_host.error().code == SdkErrorCode::InvalidArgument &&
                !wrong_scheme.ok() && wrong_scheme.error().code == SdkErrorCode::InvalidArgument &&
                !wrong_kind.ok() && wrong_kind.error().code == SdkErrorCode::InvalidArgument;
            return check(!missing.ok() && missing.error().code == SdkErrorCode::ApiResponse &&
                             missing.error().http_status == 404 &&
                             missing.error().endpoint == "/markets/slug/no-such-market",
                         "a missing market must fail with ApiResponse 404") &&
                   check(!non_binary.ok() && non_binary.error().code == SdkErrorCode::Parse,
                         "a non-binary single market must fail as Parse") &&
                   check(rejected_locally, "invalid lookups must fail with InvalidArgument") &&
                   check(server.requests().size() == 2, "invalid lookups must not send requests");
        }

        bool test_markets_pager_filters_and_cursor()
        {
            LocalServer server;
            GammaClient client(local_environment(server.url()));
            server.enqueue(gamma_fixtures::gamma_market_page);
            server.enqueue(gamma_fixtures::gamma_market_last_page);
            GammaMarketFilter filter;
            filter.closed = false;
            filter.ids = {1, 2};
            filter.liquidity_num_min = 1000.0;
            filter.order = "volume, liquidity,endDate";
            filter.slugs = {"a b", "c"};
            filter.tag_match = "any";
            auto pager = client.markets_pager(filter, 2);
            std::vector<std::string> ids;
            for (const auto &market : pager.items())
                ids.push_back(market.id);
            const auto requests = server.requests();
            const auto cursor = nlohmann::json::parse(gamma_fixtures::gamma_market_page)
                                    .at("next_cursor")
                                    .get<std::string>();
            return check(ids == std::vector<std::string>({"559651", "559652", "559651"}),
                         "markets pager must cover both pages") &&
                   check(requests.size() == 2 &&
                             requests[0].target ==
                                 "/markets/keyset?closed=false&id=1&id=2&liquidity_num_min=1000.0"
                                 "&order=volumeNum%2CliquidityNum%2CendDate&slug=a%20b&slug=c"
                                 "&tag_match=any&limit=2",
                         "markets pager must encode filters like the official SDK") &&
                   check(requests[1].target.find("&limit=2&after_cursor=") != std::string::npos &&
                             requests[1].target.find(cursor.substr(0, 20)) != std::string::npos,
                         "markets pager must follow next_cursor") &&
                   check(pager.done() && !pager.error() && pager.resume_token().exhausted,
                         "markets pager must end on a page without next_cursor");
        }

        bool test_events_pager_defaults_and_retry()
        {
            LocalServer server;
            GammaClient client(local_environment(server.url()));
            server.enqueue(R"({"error":"slow down"})", 429, {{"Retry-After", "0"}});
            server.enqueue(gamma_fixtures::gamma_event_page);
            auto events = client.events_pager().collect();
            const auto requests = server.requests();
            return check(events.ok() && events.value().size() == 1 &&
                             events.value()[0].id == "16183",
                         "events pager must parse") &&
                   check(requests.size() == 2 &&
                             requests[1].target == "/events/keyset?closed=false&limit=100",
                         "events pager must retry a 429 and list open events by default");
        }

        bool test_invalid_pagers()
        {
            LocalServer server;
            GammaClient client(local_environment(server.url()));
            GammaMarketFilter bad_match;
            bad_match.tag_match = "some";
            GammaEventFilter bad_recurrence;
            bad_recurrence.recurrence = "yearly";
            auto first = client.markets_pager(bad_match).next_page();
            auto second = client.events_pager(bad_recurrence).next_page();
            auto third = client.markets_pager({}, 0).next_page();
            return check(!first.ok() && first.error().code == SdkErrorCode::InvalidArgument &&
                             !second.ok() && second.error().code == SdkErrorCode::InvalidArgument &&
                             !third.ok() && third.error().code == SdkErrorCode::InvalidArgument,
                         "invalid pager arguments must fail with InvalidArgument") &&
                   check(server.requests().empty(), "invalid pagers must not send requests");
        }
    } // namespace
} // namespace clob_test

int main()
{
    using namespace clob_test;
    const bool passed = test_market_lookups() && test_event_lookups_and_tags() &&
                        test_lookup_failures() && test_markets_pager_filters_and_cursor() &&
                        test_events_pager_defaults_and_retry() && test_invalid_pagers();
    if (!passed) return 1;
    std::cout << "test_gamma_client passed\n";
    return 0;
}
