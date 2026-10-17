#pragma once

#include "polymarket/gamma_client.hpp"
#include "query_encoding.hpp"

#include <cstddef>
#include <exception>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

// Request plumbing shared by the GammaClient translation units.
namespace polymarket
{
    namespace detail
    {
        inline SdkError gamma_invalid_argument(const std::string &message,
                                               const std::string &endpoint)
        {
            SdkError error;
            error.code = SdkErrorCode::InvalidArgument;
            error.message = message;
            error.endpoint = endpoint;
            return error;
        }

        template <typename T, typename Parse>
        Result<Page<T>> gamma_page(const HttpResponse &response, const std::string &endpoint,
                                   Parse parse)
        {
            if (!response.ok()) return Result<Page<T>>::failure(make_sdk_error(response, endpoint));
            try
            {
                return Result<Page<T>>::success(parse(response.body));
            }
            catch (const std::exception &ex)
            {
                return Result<Page<T>>::failure(
                    make_parse_error(ex.what(), endpoint, response.body));
            }
        }

        // `query` is "" or "?a=b"; Gamma takes the page parameters after the filters.
        template <typename T, typename Get, typename ParsePage>
        Paginator<T> gamma_keyset_pager(const std::string &endpoint, const std::string &query,
                                        std::size_t page_size, const PageRequest &resume,
                                        std::optional<SdkError> invalid, Get get,
                                        ParsePage parse_page)
        {
            if (invalid)
            {
                return Paginator<T>::keyset(
                    endpoint, [error = std::move(*invalid)](const PageRequest &)
                    { return Result<Page<T>>::failure(error); }, 1, resume);
            }
            std::string prefix = endpoint + (query.empty() ? "?" : query + "&") +
                                 "limit=" + std::to_string(page_size);
            return Paginator<T>::keyset(
                endpoint,
                [endpoint, prefix = std::move(prefix), get, parse_page](const PageRequest &request)
                {
                    std::string path = prefix;
                    if (!request.cursor.empty())
                        path += "&after_cursor=" + percent_encode_query_value(request.cursor);
                    return gamma_page<T>(get(path), endpoint, parse_page);
                },
                page_size, resume);
        }

        // Parses a JSON array body into one offset page.
        template <typename T, typename ParseItems> auto gamma_list_page(ParseItems parse_items)
        {
            return [parse_items](const std::string &body)
            { return Page<T>{parse_items(body), ""}; };
        }

        // Offset pages hold at most `max_page_size` items, Gamma's own cap, and
        // start at most at `max_offset`, past which Gamma rejects the request.
        template <typename T, typename Get, typename ParsePage>
        Paginator<T> gamma_offset_pager(const std::string &endpoint, const std::string &query,
                                        std::size_t page_size, std::size_t max_page_size,
                                        std::size_t max_offset, const PageRequest &resume,
                                        std::optional<SdkError> invalid, Get get,
                                        ParsePage parse_page)
        {
            if (!invalid && (page_size == 0 || page_size > max_page_size))
                invalid = gamma_invalid_argument(
                    "page_size must be between 1 and " + std::to_string(max_page_size), endpoint);
            if (invalid)
            {
                return Paginator<T>::offset(
                    endpoint, [error = std::move(*invalid)](const PageRequest &)
                    { return Result<Page<T>>::failure(error); }, 1, max_offset);
            }
            std::string prefix = endpoint + (query.empty() ? "?" : query + "&") +
                                 "limit=" + std::to_string(page_size) + "&offset=";
            return Paginator<T>::offset(
                endpoint,
                [endpoint, prefix = std::move(prefix), get, parse_page](const PageRequest &request)
                {
                    return gamma_page<T>(get(prefix + std::to_string(request.offset)), endpoint,
                                         parse_page);
                },
                page_size, max_offset, resume.offset);
        }
    } // namespace detail

    template <typename T, typename BuildPath, typename Parse>
    Result<T> GammaClient::fetch(BuildPath build_path, Parse parse)
    {
        std::string path;
        try
        {
            path = build_path();
        }
        catch (const std::invalid_argument &ex)
        {
            return Result<T>::failure(detail::gamma_invalid_argument(ex.what(), ""));
        }
        const std::string endpoint = path.substr(0, path.find('?'));
        const auto response = get(path);
        if (!response.ok()) return Result<T>::failure(make_sdk_error(response, endpoint));
        try
        {
            return Result<T>::success(parse(response.body));
        }
        catch (const std::exception &ex)
        {
            return Result<T>::failure(make_parse_error(ex.what(), endpoint, response.body));
        }
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
            invalid = detail::gamma_invalid_argument(ex.what(), endpoint);
        }
        return detail::gamma_offset_pager<T>(
            endpoint, query, page_size, max_page_size, max_offset, resume, std::move(invalid),
            [this](const std::string &path) { return get(path); }, parse_page);
    }
} // namespace polymarket
