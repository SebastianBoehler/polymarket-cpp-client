#pragma once

#include "polymarket/gamma_client.hpp"
#include "rest_paging.hpp"

#include <cstddef>
#include <exception>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

// GammaClient member templates shared by its translation units.
namespace polymarket
{

    template <typename T, typename BuildPath, typename Parse>
    Result<T> GammaClient::fetch(BuildPath build_path, Parse parse)
    {
        return detail::fetch_result<T>(
            build_path, [this](const std::string &path) { return get(path); }, parse);
    }

    template <typename T, typename BuildQuery, typename ParsePage>
    Paginator<T> GammaClient::offset_pager(const std::string &endpoint, std::size_t page_size,
                                           std::size_t max_page_size, std::size_t max_offset,
                                           const PageRequest &resume, BuildQuery build_query,
                                           ParsePage parse_page)
    {
        std::string query;
        std::optional<SdkError> invalid;
        try
        {
            query = build_query();
        }
        catch (const std::invalid_argument &ex)
        {
            invalid = detail::invalid_argument_error(ex.what(), endpoint);
        }
        return detail::offset_pager<T>(
            endpoint, query, page_size, max_page_size, max_offset, resume, std::move(invalid),
            [this](const std::string &path) { return get(path); }, parse_page);
    }
} // namespace polymarket
