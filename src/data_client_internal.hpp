#pragma once

#include "polymarket/data_client.hpp"
#include "rest_paging.hpp"

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

// DataClient member templates shared by its translation units.
namespace polymarket
{
    namespace detail
    {
        inline constexpr std::size_t data_max_page_size = 1000;
    } // namespace detail

    template <typename T, typename BuildPath, typename Parse>
    Result<T> DataClient::fetch(BuildPath build_path, Parse parse)
    {
        return detail::fetch_result<T>(
            build_path, [this](const std::string &path) { return get(path); }, parse);
    }

    template <typename T, typename BuildQuery, typename ParsePage>
    Paginator<T> DataClient::cursor_pager(const char *endpoint, std::size_t page_size,
                                          std::size_t page_cap, const PageRequest &resume,
                                          BuildQuery build_query, ParsePage parse_page)
    {
        std::string query;
        std::optional<SdkError> invalid;
        try
        {
            if (page_size == 0 || page_size > page_cap)
                throw std::invalid_argument("page_size must be between 1 and " +
                                            std::to_string(page_cap));
            query = build_query();
        }
        catch (const std::invalid_argument &ex)
        {
            invalid = detail::invalid_argument_error(ex.what(), endpoint);
        }
        return detail::keyset_pager<T>(
            endpoint, query, page_size, resume, std::move(invalid), "cursor",
            [this](const std::string &path) { return get(path); }, parse_page);
    }
} // namespace polymarket
