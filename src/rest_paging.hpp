#pragma once

#include "polymarket/http_client.hpp"
#include "polymarket/pagination.hpp"
#include "polymarket/sdk_error.hpp"
#include "query_encoding.hpp"

#include <cstddef>
#include <exception>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

// Request and paging plumbing shared by the REST clients.
namespace polymarket
{
    namespace detail
    {
        inline SdkError invalid_argument_error(const std::string &message,
                                               const std::string &endpoint)
        {
            SdkError error;
            error.code = SdkErrorCode::InvalidArgument;
            error.message = message;
            error.endpoint = endpoint;
            return error;
        }

        template <typename T, typename Parse>
        Result<Page<T>> rest_page(const HttpResponse &response, const std::string &endpoint,
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

        // `query` is "" or "?a=b"; the page parameters follow the filters, and
        // `cursor_param` carries the server cursor after the first page.
        template <typename T, typename Get, typename ParsePage>
        Paginator<T> keyset_pager(const std::string &endpoint, const std::string &query,
                                  std::size_t page_size, const PageRequest &resume,
                                  std::optional<SdkError> invalid, const char *cursor_param,
                                  Get get, ParsePage parse_page)
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
                [endpoint, prefix = std::move(prefix), cursor_param = std::string(cursor_param),
                 get, parse_page](const PageRequest &request)
                {
                    std::string path = prefix;
                    if (!request.cursor.empty())
                        path +=
                            "&" + cursor_param + "=" + percent_encode_query_value(request.cursor);
                    return rest_page<T>(get(path), endpoint, parse_page);
                },
                page_size, resume);
        }

        // Parses a JSON array body into one offset page.
        template <typename T, typename ParseItems> auto list_page(ParseItems parse_items)
        {
            return [parse_items](const std::string &body)
            { return Page<T>{parse_items(body), ""}; };
        }

        // Offset pages hold at most `max_page_size` items, the server's own cap,
        // and start at most at `max_offset`, past which the server rejects them.
        template <typename T, typename Get, typename ParsePage>
        Paginator<T> offset_pager(const std::string &endpoint, const std::string &query,
                                  std::size_t page_size, std::size_t max_page_size,
                                  std::size_t max_offset, const PageRequest &resume,
                                  std::optional<SdkError> invalid, Get get, ParsePage parse_page)
        {
            if (!invalid && (page_size == 0 || page_size > max_page_size))
                invalid = invalid_argument_error(
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
                    return rest_page<T>(get(prefix + std::to_string(request.offset)), endpoint,
                                        parse_page);
                },
                page_size, max_offset, resume.offset);
        }

        // One request whose path is built by `build_path`; std::invalid_argument
        // from it fails with InvalidArgument before any request is sent.
        template <typename T, typename BuildPath, typename Get, typename Parse>
        Result<T> fetch_result(BuildPath build_path, Get get, Parse parse)
        {
            std::string path;
            try
            {
                path = build_path();
            }
            catch (const std::invalid_argument &ex)
            {
                return Result<T>::failure(invalid_argument_error(ex.what(), ""));
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
    } // namespace detail
} // namespace polymarket
