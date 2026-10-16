#include "check_support.hpp"
#include "pagination_test_support.hpp"
#include "polymarket/pagination.hpp"
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
    using check_support::check;
    using check_support::expect_equal;
    using pagination_test::drain;
    using pagination_test::IntPaginator;
    using pagination_test::page;
    using pagination_test::ScriptedFetch;
    using polymarket::Page;
    using polymarket::PageRequest;
    using polymarket::Result;
    using polymarket::SdkErrorCode;

    Result<Page<int>> counted_page(std::vector<int> items, size_t counted)
    {
        Page<int> result{std::move(items), ""};
        result.counted = counted;
        return Result<Page<int>>::success(std::move(result));
    }

    Result<Page<int>> numbered_page(std::vector<int> items, bool has_more)
    {
        Page<int> result{std::move(items), ""};
        result.has_more = has_more;
        return Result<Page<int>>::success(std::move(result));
    }

    void test_keyset_first_page_has_no_cursor()
    {
        ScriptedFetch fetch{{page({1, 2}, "K1"), page({3}, "")}};
        auto paginator = IntPaginator::keyset("/items/keyset", fetch.fn(), 3);
        check(drain(paginator) == std::vector<int>({1, 2, 3}),
              "keyset items must cover every page");
        check(fetch.requests.size() == 2, "empty next cursor must end keyset paging");
        expect_equal("keyset first cursor", fetch.requests[0].cursor, "");
        expect_equal("keyset second cursor", fetch.requests[1].cursor, "K1");
        check(fetch.requests[0].limit == 3 && fetch.requests[1].limit == 3,
              "keyset requests must carry the limit");
        check(paginator.done() && !paginator.error(), "keyset must end cleanly");
    }

    void test_keyset_resume_tokens()
    {
        ScriptedFetch fetch{{page({1}, "K1"), page({2}, "")}};
        auto paginator = IntPaginator::keyset("/items/keyset", fetch.fn(), 5);
        (void)paginator.next_page();
        const auto middle = paginator.resume_token();
        check(middle.cursor == "K1" && middle.limit == 5 && !middle.exhausted,
              "keyset token after a middle page must hold the server cursor");

        (void)paginator.next_page();
        const auto last = paginator.resume_token();
        check(last.cursor.empty() && last.exhausted && paginator.done(),
              "keyset token must be exhausted right after the last page");

        ScriptedFetch resumed_fetch{{page({2}, "")}};
        auto resumed = IntPaginator::keyset("/items/keyset", resumed_fetch.fn(), 5, middle);
        check(drain(resumed) == std::vector<int>({2}) && resumed_fetch.requests[0].cursor == "K1",
              "keyset must resume at the saved cursor");

        ScriptedFetch finished_fetch;
        auto finished = IntPaginator::keyset("/items/keyset", finished_fetch.fn(), 5, last);
        check(drain(finished).empty() && finished_fetch.requests.empty() && finished.done(),
              "an exhausted keyset token must not fetch the first page again");
    }

    void test_keyset_cycle_and_limit()
    {
        ScriptedFetch fetch{{page({1}, "A"), page({2}, "A")}};
        auto paginator = IntPaginator::keyset("/items/keyset", fetch.fn(), 2);
        auto collected = paginator.collect();
        check(!collected.ok() && collected.error().code == SdkErrorCode::Parse,
              "keyset cursor cycle must fail as parse");
        check(fetch.requests.size() == 2 && !paginator.resume_token().exhausted,
              "failed keyset must not fetch again or report exhaustion");

        ScriptedFetch unused;
        check_support::expect_throws<std::invalid_argument>(
            "keyset zero limit",
            [&] { (void)IntPaginator::keyset("/items/keyset", unused.fn(), 0); });
    }

    void test_offset_counts_top_level_rows()
    {
        ScriptedFetch fetch{{counted_page({1, 10, 2}, 2), counted_page({3, 30}, 1)}};
        auto paginator = IntPaginator::offset("/comments", fetch.fn(), 2, 200);
        check(drain(paginator) == std::vector<int>({1, 10, 2, 3, 30}),
              "counted pages must yield every row");
        check(fetch.requests.size() == 2 && fetch.requests[1].offset == 2,
              "offset must advance by the counted rows");
        check(paginator.resume_token().offset == 3,
              "resume offset must count only the counted rows");
    }

    void test_offset_rejects_bad_counts()
    {
        ScriptedFetch over_limit{{counted_page({1, 2, 3}, 3)}};
        auto first = IntPaginator::offset("/comments", over_limit.fn(), 2, 200).next_page();
        check(!first.ok() && first.error().code == SdkErrorCode::Parse,
              "count above limit must fail");

        ScriptedFetch over_items{{counted_page({1}, 2)}};
        auto second = IntPaginator::offset("/comments", over_items.fn(), 2, 200).next_page();
        check(!second.ok() && second.error().code == SdkErrorCode::Parse,
              "count above items must fail");
    }

    void test_numbered_follows_has_more()
    {
        ScriptedFetch fetch{{numbered_page({1}, true), numbered_page({2}, false)}};
        auto paginator = IntPaginator::numbered("/public-search", fetch.fn(), 4);
        check(drain(paginator) == std::vector<int>({1, 2}), "numbered items must cover every page");
        check(fetch.requests.size() == 2 && fetch.requests[0].page == 1 &&
                  fetch.requests[1].page == 2,
              "numbered pages must count from 1");
        check(fetch.requests[0].limit == 4, "numbered requests must carry the limit");
        const auto token = paginator.resume_token();
        check(token.page == 3 && token.exhausted && paginator.done() && !paginator.error(),
              "numbered token must be exhausted after a page without more results");

        ScriptedFetch finished_fetch;
        auto finished = IntPaginator::numbered("/public-search", finished_fetch.fn(), 4, token);
        check(drain(finished).empty() && finished_fetch.requests.empty(),
              "an exhausted numbered token must not fetch again");
    }

    void test_numbered_resume_and_page_cap()
    {
        ScriptedFetch fetch{{numbered_page({5}, false)}};
        PageRequest start;
        start.page = 5;
        auto resumed = IntPaginator::numbered("/public-search", fetch.fn(), 4, start);
        check(drain(resumed) == std::vector<int>({5}) && fetch.requests[0].page == 5,
              "numbered must resume at the saved page");

        size_t requests = 0;
        auto endless = IntPaginator::numbered(
            "/public-search",
            [&](const PageRequest &)
            {
                ++requests;
                return numbered_page({1}, true);
            },
            4);
        auto collected = endless.collect();
        check(!collected.ok() && collected.error().code == SdkErrorCode::Parse,
              "numbered page cap must fail");
        check(requests == polymarket::detail::OpaqueCursorPagination::max_pages,
              "numbered page cap must stop requests at max_pages");
    }
} // namespace

int main()
{
    test_keyset_first_page_has_no_cursor();
    test_keyset_resume_tokens();
    test_keyset_cycle_and_limit();
    test_offset_counts_top_level_rows();
    test_offset_rejects_bad_counts();
    test_numbered_follows_has_more();
    test_numbered_resume_and_page_cap();
    return check_support::finish("test_pagination_strategies");
}
