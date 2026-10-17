#include "polymarket/data_client.hpp"

#include "data_parsing.hpp"
#include "data_query.hpp"
#include "rate_limit_internal.hpp"
#include "rest_paging.hpp"

#include <stdexcept>
#include <string>
#include <utility>

namespace polymarket
{
    namespace
    {
        constexpr std::size_t max_page_size = 1000;
    } // namespace

    DataClient::DataClient(Environment environment,
                           const std::optional<HttpClientOptions> &http_options)
        : environment_(std::move(environment))
    {
        environment_.validate();
        if (http_options)
            http_.configure(*http_options);
        else
            http_.set_timeout_ms(10000);
        http_.set_base_url(environment_.data_url);
    }

    void DataClient::set_rate_limit_retry(std::optional<RateLimitRetry> policy)
    {
        if (policy) detail::validate_rate_limit_retry(*policy);
        std::lock_guard<std::mutex> lock(rate_limit_mutex_);
        rate_limit_retry_ = policy;
    }

    HttpResponse DataClient::get(const std::string &path)
    {
        std::optional<RateLimitRetry> policy;
        {
            std::lock_guard<std::mutex> lock(rate_limit_mutex_);
            policy = rate_limit_retry_;
        }
        return detail::retry_rate_limited(policy, [&] { return http_.get(path); });
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

    Paginator<DataTrade> DataClient::trades_pager(const DataTradeFilter &filter,
                                                  std::size_t page_size, const PageRequest &resume)
    {
        return cursor_pager<DataTrade>(
            "/v2/trades", page_size, max_page_size, resume,
            [&] { return detail::data_trade_query(filter); }, detail::parse_data_trade_page);
    }

    Paginator<DataActivity> DataClient::activity_pager(const std::string &user,
                                                       const DataActivityFilter &filter,
                                                       std::size_t page_size,
                                                       const PageRequest &resume)
    {
        return cursor_pager<DataActivity>(
            "/v2/activity", page_size, max_page_size, resume,
            [&] { return detail::data_activity_query(user, filter); },
            detail::parse_data_activity_page);
    }
} // namespace polymarket
