#include "polymarket/gamma_client.hpp"

#include "gamma_parsing.hpp"
#include "gamma_query.hpp"
#include "query_encoding.hpp"
#include "rate_limit_internal.hpp"

#include <exception>
#include <stdexcept>
#include <string>
#include <utility>

namespace polymarket
{
    namespace
    {
        SdkError invalid_argument_error(const std::string &message, const std::string &endpoint)
        {
            SdkError error;
            error.code = SdkErrorCode::InvalidArgument;
            error.message = message;
            error.endpoint = endpoint;
            return error;
        }

        template <typename T, typename Get, typename ParsePage>
        Paginator<T> keyset_pager(const std::string &endpoint, const std::string &query,
                                  std::size_t page_size, const PageRequest &resume,
                                  std::optional<SdkError> invalid, Get get, ParsePage parse_page)
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
                [endpoint, prefix = std::move(prefix), get,
                 parse_page](const PageRequest &request) -> Result<Page<T>>
                {
                    std::string path = prefix;
                    if (!request.cursor.empty())
                        path +=
                            "&after_cursor=" + detail::percent_encode_query_value(request.cursor);
                    const auto response = get(path);
                    if (!response.ok())
                        return Result<Page<T>>::failure(make_sdk_error(response, endpoint));
                    try
                    {
                        return Result<Page<T>>::success(parse_page(response.body));
                    }
                    catch (const std::exception &ex)
                    {
                        return Result<Page<T>>::failure(
                            make_parse_error(ex.what(), endpoint, response.body));
                    }
                },
                page_size, resume);
        }
    } // namespace

    GammaClient::GammaClient(Environment environment,
                             const std::optional<HttpClientOptions> &http_options)
        : environment_(std::move(environment))
    {
        environment_.validate();
        if (http_options)
            http_.configure(*http_options);
        else
            http_.set_timeout_ms(10000);
        http_.set_base_url(environment_.gamma_url);
    }

    void GammaClient::set_rate_limit_retry(std::optional<RateLimitRetry> policy)
    {
        if (policy) detail::validate_rate_limit_retry(*policy);
        std::lock_guard<std::mutex> lock(rate_limit_mutex_);
        rate_limit_retry_ = policy;
    }

    HttpResponse GammaClient::get(const std::string &path)
    {
        std::optional<RateLimitRetry> policy;
        {
            std::lock_guard<std::mutex> lock(rate_limit_mutex_);
            policy = rate_limit_retry_;
        }
        return detail::retry_rate_limited(policy, [&] { return http_.get(path); });
    }

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

    Result<GammaMarket> GammaClient::get_market_by_id(const std::string &id,
                                                      const GammaMarketLookupOptions &options)
    {
        return fetch<GammaMarket>(
            [&]
            {
                return detail::gamma_id_path("market", id) +
                       detail::gamma_market_lookup_query(options);
            },
            detail::parse_gamma_market_response);
    }

    Result<GammaMarket> GammaClient::get_market_by_slug(const std::string &slug,
                                                        const GammaMarketLookupOptions &options)
    {
        return fetch<GammaMarket>(
            [&]
            {
                return detail::gamma_slug_path("market", slug) +
                       detail::gamma_market_lookup_query(options);
            },
            detail::parse_gamma_market_response);
    }

    Result<GammaMarket> GammaClient::get_market_by_url(const std::string &url,
                                                       const GammaMarketLookupOptions &options)
    {
        return fetch<GammaMarket>(
            [&]
            {
                return detail::gamma_slug_path("market",
                                               detail::gamma_slug_from_url("market", url)) +
                       detail::gamma_market_lookup_query(options);
            },
            detail::parse_gamma_market_response);
    }

    Result<std::vector<GammaTagReference>>
    GammaClient::get_market_tags(const std::string &market_id)
    {
        return fetch<std::vector<GammaTagReference>>(
            [&] { return detail::gamma_id_path("market", market_id) + "/tags"; },
            detail::parse_gamma_tag_references);
    }

    Result<GammaEvent> GammaClient::get_event_by_id(const std::string &id,
                                                    const GammaEventLookupOptions &options)
    {
        return fetch<GammaEvent>(
            [&]
            {
                return detail::gamma_id_path("event", id) +
                       detail::gamma_event_lookup_query(options);
            },
            detail::parse_gamma_event_response);
    }

    Result<GammaEvent> GammaClient::get_event_by_slug(const std::string &slug,
                                                      const GammaEventLookupOptions &options)
    {
        return fetch<GammaEvent>(
            [&]
            {
                return detail::gamma_slug_path("event", slug) +
                       detail::gamma_event_lookup_query(options);
            },
            detail::parse_gamma_event_response);
    }

    Result<GammaEvent> GammaClient::get_event_by_url(const std::string &url,
                                                     const GammaEventLookupOptions &options)
    {
        return fetch<GammaEvent>(
            [&]
            {
                return detail::gamma_slug_path("event", detail::gamma_slug_from_url("event", url)) +
                       detail::gamma_event_lookup_query(options);
            },
            detail::parse_gamma_event_response);
    }

    Result<std::vector<GammaTagReference>> GammaClient::get_event_tags(const std::string &event_id)
    {
        return fetch<std::vector<GammaTagReference>>(
            [&] { return detail::gamma_id_path("event", event_id) + "/tags"; },
            detail::parse_gamma_tag_references);
    }

    Paginator<GammaMarket> GammaClient::markets_pager(const GammaMarketFilter &filter,
                                                      std::size_t page_size,
                                                      const PageRequest &resume)
    {
        constexpr const char *endpoint = "/markets/keyset";
        std::string query;
        std::optional<SdkError> invalid;
        try
        {
            if (page_size == 0) throw std::invalid_argument("page_size must be positive");
            query = detail::gamma_market_list_query(filter);
        }
        catch (const std::invalid_argument &ex)
        {
            invalid = invalid_argument_error(ex.what(), endpoint);
        }
        return keyset_pager<GammaMarket>(
            endpoint, query, page_size, resume, std::move(invalid), [this](const std::string &path)
            { return get(path); }, detail::parse_gamma_market_keyset_page);
    }

    Paginator<GammaEvent> GammaClient::events_pager(const GammaEventFilter &filter,
                                                    std::size_t page_size,
                                                    const PageRequest &resume)
    {
        constexpr const char *endpoint = "/events/keyset";
        std::string query;
        std::optional<SdkError> invalid;
        try
        {
            if (page_size == 0) throw std::invalid_argument("page_size must be positive");
            query = detail::gamma_event_list_query(filter);
        }
        catch (const std::invalid_argument &ex)
        {
            invalid = invalid_argument_error(ex.what(), endpoint);
        }
        return keyset_pager<GammaEvent>(
            endpoint, query, page_size, resume, std::move(invalid), [this](const std::string &path)
            { return get(path); }, detail::parse_gamma_event_keyset_page);
    }
} // namespace polymarket
