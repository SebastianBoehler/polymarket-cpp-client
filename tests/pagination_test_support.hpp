#pragma once

#include "polymarket/pagination.hpp"
#include <cstddef>
#include <iterator>
#include <ranges>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace pagination_test
{
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

    inline Result<Page<int>> page(std::vector<int> items, std::string next_cursor = "")
    {
        return Result<Page<int>>::success(Page<int>{std::move(items), std::move(next_cursor)});
    }

    inline Result<Page<int>> http_failure()
    {
        SdkError error;
        error.code = SdkErrorCode::HttpTransport;
        error.message = "connection reset";
        error.endpoint = "/items";
        return Result<Page<int>>::failure(error);
    }

    inline std::vector<int> drain(IntPaginator &paginator)
    {
        std::vector<int> items;
        for (int item : paginator.items())
            items.push_back(item);
        return items;
    }
} // namespace pagination_test
