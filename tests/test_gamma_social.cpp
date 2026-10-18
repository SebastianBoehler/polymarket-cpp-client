#include "../src/clob_client_test_fixture.hpp"
#include "gamma_parsing.hpp"
#include "gamma_social_fixtures.hpp"
#include "polymarket/gamma_client.hpp"

#include <functional>
#include <string>
#include <vector>

namespace clob_test
{
    namespace
    {
        namespace fixtures = gamma_social_fixtures;
        const std::string user = "0x1111111111111111111111111111111111111111";

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

        bool test_recorded_parsing()
        {
            const auto page =
                detail::parse_gamma_comment_keyset_page(fixtures::gamma_comment_keyset_page);
            const auto &comment = page.items.at(0);
            const auto thread = detail::parse_gamma_comments(fixtures::gamma_comment_thread);
            const auto profile = detail::parse_gamma_public_profile(fixtures::gamma_public_profile);
            const auto search = detail::parse_gamma_search_page(fixtures::gamma_search_page);
            const auto &results = search.items.at(0);
            return check(page.items.size() == 3 && !page.next_cursor.empty(),
                         "comment keyset page must parse") &&
                   check(comment.id == "3276874" && comment.parent_entity_id == "16183" &&
                             comment.parent_entity_type == "Event" &&
                             comment.parent_comment_id.empty() && comment.user_address == user &&
                             comment.reaction_count == 3,
                         "comment fields must parse with numeric ids as strings") &&
                   check(comment.profile &&
                             comment.profile->wallet ==
                                 "0x2222222222222222222222222222222222222222" &&
                             comment.profile->base_address == user,
                         "comment profile wallet must fall back to proxyWallet") &&
                   check(!comment.reactions.empty() &&
                             comment.reactions[0].reaction_type == "HEART" &&
                             comment.reactions[0].comment_id == 3276874 &&
                             comment.reactions[0].profile,
                         "comment reactions must parse") &&
                   check(thread.size() == 1 && thread[0].id == "3276874",
                         "comment thread must parse") &&
                   check(profile.wallet == "0x2222222222222222222222222222222222222222" &&
                             profile.users.size() == 1 && profile.users[0].id == "1001" &&
                             profile.verified_badge == false && profile.raw.contains("takerTier"),
                         "public profile must parse and keep raw fields") &&
                   check(search.has_more && results.total_results == 855529 &&
                             results.events.size() == 1 && results.events[0].id == "300564" &&
                             results.tags.size() == 1 && results.tags[0].event_count == 360 &&
                             results.profiles.size() == 1,
                         "search page must parse with pagination");
        }

        bool test_edge_parsing()
        {
            const auto roots = detail::parse_gamma_comment_root_page(
                R"([{"id":"1"},{"id":"2","parentCommentID":1},{"id":3,"parentCommentID":null}])");
            const auto wallet =
                detail::parse_gamma_public_profile(R"({"wallet":"0xaa","proxyWallet":"0xbb"})");
            const auto bare = detail::parse_gamma_search_page(R"({"events":null})");
            return check(roots.items.size() == 3 && roots.counted == 2 &&
                             roots.items[1].parent_comment_id == "1",
                         "offset comment pages must count only top-level comments") &&
                   check(wallet.wallet == "0xaa", "an explicit wallet must win over proxyWallet") &&
                   check(!bare.has_more && bare.items.size() == 1 && bare.items[0].events.empty(),
                         "search without pagination must end paging") &&
                   check(rejected(
                             []
                             {
                                 (void)detail::parse_gamma_search_page(
                                     R"({"pagination":{"hasMore":"yes"}})");
                             }),
                         "a non-boolean hasMore must be rejected") &&
                   check(rejected([] { (void)detail::parse_gamma_search_page(R"({"tags":{}})"); }),
                         "non-array search results must be rejected") &&
                   check(rejected([] { (void)detail::parse_gamma_comments(R"([{"body":"x"}])"); }),
                         "a comment without id must be rejected") &&
                   check(
                       detail::parse_gamma_comments(
                           R"([{"id":"1","profile":{"positions":[{"tokenId":"7","positionSize":"47612999494"}]}}])")
                               .at(0)
                               .profile->positions.at(0)
                               .position_size == 47612999494,
                       "a digit-string position size must parse") &&
                   check(
                       rejected(
                           []
                           {
                               (void)detail::parse_gamma_comments(
                                   R"([{"id":"1","profile":{"positions":[{"positionSize":"4.5"}]}}])");
                           }),
                       "a fractional position size must be rejected") &&
                   check(rejected([] { (void)detail::parse_gamma_public_profile(R"([])"); }),
                         "a non-object profile must be rejected");
        }

        bool test_search_pager()
        {
            LocalServer server;
            GammaClient client(local_environment(server.url()));
            server.enqueue(fixtures::gamma_search_page);
            server.enqueue(R"({"events":[],"pagination":{"hasMore":false,"totalResults":1}})");
            GammaSearchOptions options;
            options.exclude_tag_ids = {1};
            options.search_tags = true;
            options.sort = "volume";
            auto pager = client.search_pager("crypto", options, 2);
            auto pages = pager.collect();
            const auto requests = server.requests();
            GammaSearchOptions bad_sort;
            bad_sort.sort = "random";
            auto empty_query = client.search_pager("").next_page();
            auto invalid_sort = client.search_pager("x", bad_sort).next_page();
            auto zero_size = client.search_pager("x", {}, 0).next_page();
            return check(pages.ok() && pages.value().size() == 2,
                         "search pager must return one result per page") &&
                   check(
                       requests.size() == 2 &&
                           requests[0].target == "/public-search?q=crypto&exclude_tag_id=1&search_"
                                                 "tags=true&sort=volume&page=1&limit_per_type=2" &&
                           requests[1].target.find("&page=2&limit_per_type=2") != std::string::npos,
                       "search pager must number pages from 1") &&
                   check(pager.resume_token().exhausted && pager.resume_token().page == 3,
                         "search pager must end when hasMore is false") &&
                   check(!empty_query.ok() &&
                             empty_query.error().code == SdkErrorCode::InvalidArgument &&
                             !invalid_sort.ok() && !zero_size.ok() && server.requests().size() == 2,
                         "invalid searches must fail before any request");
        }

        bool test_comment_keyset_pager()
        {
            LocalServer server;
            GammaClient client(local_environment(server.url()));
            server.enqueue(fixtures::gamma_comment_keyset_page);
            server.enqueue(R"({"comments":[]})");
            server.enqueue(R"({"comments":[]})");
            server.enqueue(R"({"comments":[]})");
            auto comments = client.comments_pager("16183", "Event").collect();
            GammaCommentFilter by_id;
            by_id.order = "id";
            (void)client.comments_pager("16183", "Series", by_id, 5).collect();
            by_id.ascending = false;
            (void)client.comments_pager("16183", "Series", by_id, 5).collect();
            const auto requests = server.requests();
            const auto cursor = nlohmann::json::parse(fixtures::gamma_comment_keyset_page)
                                    .at("next_cursor")
                                    .get<std::string>();
            return check(comments.ok() && comments.value().size() == 3,
                         "comment keyset pager must parse") &&
                   check(requests.size() == 4 &&
                             requests[0].target ==
                                 "/comments/keyset?parent_entity_id=16183&parent_entity_type=Event"
                                 "&order=createdAt&ascending=false&limit=20",
                         "a plain comment read must page newest first by cursor") &&
                   check(requests[1].target.find("&limit=20&after_cursor=" +
                                                 cursor.substr(0, 16)) != std::string::npos,
                         "comment keyset pager must follow next_cursor") &&
                   check(requests[2].target.find("&order=id&ascending=true&limit=5") !=
                                 std::string::npos &&
                             requests[3].target.find("&order=id&ascending=false&limit=5") !=
                                 std::string::npos,
                         "an explicit order must default to ascending");
        }

        bool test_comment_offset_pagers()
        {
            LocalServer server;
            GammaClient client(local_environment(server.url()));
            server.enqueue(R"([{"id":"1"},{"id":"2","parentCommentID":1},{"id":"3"}])");
            server.enqueue(R"([{"id":"4"}])");
            server.enqueue(R"([{"id":"9"}])");
            server.enqueue(
                R"([{"id":"5","userAddress":"0x1111111111111111111111111111111111111111"}])");
            GammaCommentFilter positions;
            positions.get_positions = true;
            auto with_positions = client.comments_pager("16183", "Event", positions, 2).collect();
            GammaCommentFilter holders;
            holders.holders_only = true;
            PageRequest deep;
            deep.offset = 200;
            auto capped = client.comments_pager("16183", "Event", holders, 1, deep);
            auto capped_items = capped.collect();
            GammaUserCommentFilter user_filter;
            user_filter.order = "createdAt";
            auto by_user = client.user_comments_pager(user, user_filter, 1, deep).collect();
            const auto requests = server.requests();
            return check(with_positions.ok() && with_positions.value().size() == 4,
                         "offset comment pages must keep replies") &&
                   check(requests.size() == 4 &&
                             requests[0].target ==
                                 "/comments?parent_entity_id=16183&parent_entity_type=Event"
                                 "&get_positions=true&limit=2&offset=0" &&
                             requests[1].target.find("&limit=2&offset=2") != std::string::npos,
                         "offset comment pages must advance by top-level comments") &&
                   check(capped_items.ok() && capped.truncated() &&
                             requests[2].target.find("&offset=200") != std::string::npos,
                         "comment offset paging must stop with truncated() at offset 200") &&
                   check(by_user.ok() &&
                             requests[3].target == "/comments/user_address/" + user +
                                                       "?order=createdAt&limit=1&offset=200",
                         "user comments must use the address path and stop at the cap");
        }

        bool test_comment_and_profile_lookups()
        {
            LocalServer server;
            GammaClient client(local_environment(server.url()));
            server.enqueue(fixtures::gamma_comment_thread);
            server.enqueue(fixtures::gamma_public_profile);
            server.enqueue(R"({"type":"not found error","error":"profile not found"})", 404);
            server.enqueue(R"({"error":"boom"})", 500);
            auto thread = client.get_comment_thread("3276874", true);
            auto profile = client.get_public_profile(user);
            auto missing = client.get_public_profile(user);
            auto failed = client.get_public_profile(user);
            auto bad_type = client.comments_pager("16183", "Market").next_page();
            auto oversized = client.comments_pager("16183", "Event", {}, 101).next_page();
            auto no_address = client.user_comments_pager("").next_page();
            auto no_profile_address = client.get_public_profile("");
            const auto requests = server.requests();
            return check(thread.ok() && thread.value().size() == 1 && profile.ok() &&
                             profile.value().has_value(),
                         "comment thread and profile lookups must parse") &&
                   check(requests.size() == 4 &&
                             requests[0].target == "/comments/3276874?get_positions=true" &&
                             requests[1].target == "/public-profile?address=" + user,
                         "comment thread and profile lookups must use their paths") &&
                   check(missing.ok() && !missing.value().has_value(),
                         "a 404 profile must be an empty result") &&
                   check(!failed.ok() && failed.error().http_status == 500,
                         "other profile failures must be errors") &&
                   check(!bad_type.ok() && bad_type.error().code == SdkErrorCode::InvalidArgument &&
                             !oversized.ok() && !no_address.ok() && !no_profile_address.ok(),
                         "invalid comment and profile arguments must fail before any request");
        }
    } // namespace
} // namespace clob_test

int main()
{
    using namespace clob_test;
    const bool passed = test_recorded_parsing() && test_edge_parsing() && test_search_pager() &&
                        test_comment_keyset_pager() && test_comment_offset_pagers() &&
                        test_comment_and_profile_lookups();
    if (!passed) return 1;
    std::cout << "test_gamma_social passed\n";
    return 0;
}
