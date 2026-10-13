#include "check_support.hpp"
#include "polymarket/pagination.hpp"
#include <cstddef>
#include <iterator>
#include <ranges>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
    using check_support::check;
    using check_support::expect_equal;
    using polymarket::Page;
    using polymarket::PageRequest;
    using polymarket::Paginator;
    using polymarket::Result;
    using polymarket::SdkError;
    using polymarket::SdkErrorCode;

    using IntPaginator = Paginator<int>;
    static_assert(std::input_iterator<IntPaginator::ItemIterator>);
    static_assert(std::ranges::input_range<IntPaginator::ItemRange>);

    struct ScriptedFetch
    {
        std::vector<Result<Page<int>>> pages;
        std::vector<PageRequest> requests;
        size_t next{0};

        IntPaginator::Fetch fn()
        {
            return [this](const PageRequest &request)
            {
                requests.push_back(request);
                if (next >= pages.size()) throw std::logic_error("unexpected page request");
                return pages[next++];
            };
        }
    };

    Result<Page<int>> page(std::vector<int> items, std::string next_cursor = "")
    {
        return Result<Page<int>>::success(Page<int>{std::move(items), std::move(next_cursor)});
    }

    Result<Page<int>> http_failure()
    {
        SdkError error;
        error.code = SdkErrorCode::HttpTransport;
        error.message = "connection reset";
        error.endpoint = "/items";
        return Result<Page<int>>::failure(error);
    }

    std::vector<int> drain(IntPaginator &paginator)
    {
        std::vector<int> items;
        for (int item : paginator.items())
            items.push_back(item);
        return items;
    }

    void test_cursor_follows_until_end_cursor()
    {
        ScriptedFetch fetch{{page({1, 2}, "A"), page({3}, "LTE=")}};
        auto paginator = IntPaginator::cursor("/items", fetch.fn(), "MA==", "LTE=");
        check(drain(paginator) == std::vector<int>({1, 2, 3}),
              "cursor items must cover every page");
        check(fetch.requests.size() == 2, "cursor must stop at the end cursor");
        expect_equal("first cursor", fetch.requests[0].cursor, "MA==");
        expect_equal("second cursor", fetch.requests[1].cursor, "A");
        check(paginator.done() && !paginator.error() && !paginator.truncated(),
              "cursor must end cleanly");
        auto after = paginator.next_page();
        check(after.ok() && !after.value(), "exhausted cursor must return nullopt");
    }

    void test_cursor_empty_next_cursor_ends()
    {
        ScriptedFetch fetch{{page({1}, "")}};
        auto paginator = IntPaginator::cursor("/items", fetch.fn(), "MA==", "LTE=");
        auto collected = paginator.collect();
        check(collected.ok() && collected.value() == std::vector<int>({1}),
              "empty cursor must end paging");
        check(fetch.requests.size() == 1, "empty cursor must not request again");
    }

    void test_cursor_cycle_fails()
    {
        ScriptedFetch fetch{{page({1}, "A"), page({2}, "A")}};
        auto paginator = IntPaginator::cursor("/items", fetch.fn(), "MA==", "LTE=");
        auto first = paginator.next_page();
        check(first.ok() && first.value() && *first.value() == std::vector<int>({1}),
              "first page must load");
        auto second = paginator.next_page();
        check(second.ok() && second.value() && *second.value() == std::vector<int>({2}),
              "page that repeats a cursor must still be returned");
        check(paginator.error().has_value() && paginator.done(), "cycle must set error()");
        auto third = paginator.next_page();
        check(!third.ok() && third.error().code == SdkErrorCode::Parse,
              "call after a cursor cycle must fail as parse");
        expect_equal("cycle endpoint", third.error().endpoint, "/items");
        check(fetch.requests.size() == 2, "cursor cycle must not fetch again");
    }

    void test_cursor_page_cap_fails()
    {
        size_t requests = 0;
        auto paginator = IntPaginator::cursor(
            "/items",
            [&](const PageRequest &)
            {
                ++requests;
                return page({1}, "C" + std::to_string(requests));
            },
            "MA==", "LTE=");
        auto collected = paginator.collect();
        check(!collected.ok() && collected.error().code == SdkErrorCode::Parse,
              "page cap must fail");
        check(requests == polymarket::detail::OpaqueCursorPagination::max_pages,
              "page cap must stop requests at max_pages");
    }

    void test_offset_stops_on_short_page()
    {
        ScriptedFetch fetch{{page({1, 2}), page({3, 4}), page({5})}};
        auto paginator = IntPaginator::offset("/items", fetch.fn(), 2, 10'000);
        check(drain(paginator) == std::vector<int>({1, 2, 3, 4, 5}),
              "offset items must cover every page");
        check(fetch.requests.size() == 3, "short page must end offset paging");
        check(fetch.requests[0].offset == 0 && fetch.requests[1].offset == 2 &&
                  fetch.requests[2].offset == 4,
              "offset must advance by limit");
        check(fetch.requests[0].limit == 2, "offset request must carry the limit");
        check(!paginator.truncated() && !paginator.error(), "short page is a clean end");
    }

    void test_offset_cap_sets_truncated()
    {
        ScriptedFetch fetch{{page({1, 2}), page({3, 4})}};
        auto paginator = IntPaginator::offset("/items", fetch.fn(), 2, 2);
        auto collected = paginator.collect();
        check(collected.ok() && collected.value() == std::vector<int>({1, 2, 3, 4}),
              "pages up to the cap load");
        check(fetch.requests.size() == 2 && fetch.requests[1].offset == 2,
              "max_offset itself is requested");
        check(paginator.truncated() && paginator.done() && !paginator.error(),
              "offset cap must set truncated()");
    }

    void test_offset_oversized_page_fails()
    {
        ScriptedFetch fetch{{page({1, 2, 3})}};
        auto paginator = IntPaginator::offset("/items", fetch.fn(), 2, 10'000);
        auto first = paginator.next_page();
        check(!first.ok() && first.error().code == SdkErrorCode::Parse,
              "page above limit must fail");
    }

    void test_offset_rejects_zero_limit()
    {
        ScriptedFetch fetch;
        check_support::expect_throws<std::invalid_argument>(
            "zero limit", [&] { (void)IntPaginator::offset("/items", fetch.fn(), 0, 10); });
        check_support::expect_throws<std::invalid_argument>(
            "missing fetch", [] { (void)IntPaginator::cursor("/items", {}, "MA==", "LTE="); });
    }

    void test_failure_keeps_earlier_pages_and_sticks()
    {
        ScriptedFetch fetch{{page({1, 2}, "A"), http_failure()}};
        auto paginator = IntPaginator::cursor("/items", fetch.fn(), "MA==", "LTE=");
        check(drain(paginator) == std::vector<int>({1, 2}),
              "items before a failure must be yielded");
        check(paginator.error() && paginator.error()->code == SdkErrorCode::HttpTransport,
              "range-for must expose the fetch error");
        auto again = paginator.next_page();
        check(!again.ok() && again.error().message == "connection reset", "failure must be sticky");
        check(fetch.requests.size() == 2, "failed paginator must not fetch again");
    }

    void test_collect_reports_failure()
    {
        ScriptedFetch fetch{{page({1}), http_failure()}};
        auto paginator = IntPaginator::offset("/items", fetch.fn(), 1, 10);
        auto collected = paginator.collect();
        check(!collected.ok() && collected.error().code == SdkErrorCode::HttpTransport,
              "collect must fail when a later page fails");
    }

    void test_collect_max_items()
    {
        ScriptedFetch fetch{{page({1, 2}), page({3, 4}), page({5})}};
        auto paginator = IntPaginator::offset("/items", fetch.fn(), 2, 10'000);
        auto collected = paginator.collect(3);
        check(collected.ok() && collected.value() == std::vector<int>({1, 2, 3}),
              "collect must stop at max_items");
        check(fetch.requests.size() == 2, "collect must not fetch past max_items");
        check(paginator.resume_token().offset == 4,
              "resume token must point past the last fetched page");
    }

    void test_resume_tokens()
    {
        ScriptedFetch cursor_fetch{{page({1}, "B"), page({2}, "LTE=")}};
        auto cursor = IntPaginator::cursor("/items", cursor_fetch.fn(), "MA==", "LTE=");
        (void)cursor.next_page();
        const auto saved = cursor.resume_token();
        expect_equal("saved cursor", saved.cursor, "B");
        auto resumed = IntPaginator::cursor("/items", cursor_fetch.fn(), saved.cursor, "LTE=");
        check(drain(resumed) == std::vector<int>({2}), "resumed cursor must continue");

        ScriptedFetch offset_fetch{{page({7})}};
        auto offset = IntPaginator::offset("/items", offset_fetch.fn(), 2, 10, 4);
        check(drain(offset) == std::vector<int>({7}), "offset must start from start_offset");
        check(offset_fetch.requests[0].offset == 4, "start_offset must be requested first");

        ScriptedFetch past_cap;
        auto beyond = IntPaginator::offset("/items", past_cap.fn(), 2, 10, 12);
        check(beyond.truncated() && drain(beyond).empty() && past_cap.requests.empty(),
              "start past max_offset must not fetch");
    }

    void test_range_skips_empty_pages_and_resumes_after_break()
    {
        ScriptedFetch fetch{
            {page({}, "A"), page({1, 2, 3}, "B"), page({}, "C"), page({4}, "LTE=")}};
        auto paginator = IntPaginator::cursor("/items", fetch.fn(), "MA==", "LTE=");
        std::vector<int> seen;
        for (int item : paginator.items())
        {
            seen.push_back(item);
            if (item == 2) break;
        }
        for (int item : paginator.items())
            seen.push_back(item);
        check(seen == std::vector<int>({1, 2, 2, 3, 4}),
              "range must skip empty pages and resume at the break");
        check(paginator.done() && !paginator.error(), "range must end cleanly");
    }
} // namespace

int main()
{
    test_cursor_follows_until_end_cursor();
    test_cursor_empty_next_cursor_ends();
    test_cursor_cycle_fails();
    test_cursor_page_cap_fails();
    test_offset_stops_on_short_page();
    test_offset_cap_sets_truncated();
    test_offset_oversized_page_fails();
    test_offset_rejects_zero_limit();
    test_failure_keeps_earlier_pages_and_sticks();
    test_collect_reports_failure();
    test_collect_max_items();
    test_resume_tokens();
    test_range_skips_empty_pages_and_resumes_after_break();
    return check_support::finish("test_pagination");
}
