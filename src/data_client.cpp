#include "polymarket/data_client.hpp"

#include "data_client_internal.hpp"
#include "data_parsing.hpp"
#include "data_query.hpp"
#include "rate_limit_internal.hpp"

#include <string>
#include <utility>

namespace polymarket
{
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

    Paginator<DataTrade> DataClient::trades_pager(const DataTradeFilter &filter,
                                                  std::size_t page_size, const PageRequest &resume)
    {
        return cursor_pager<DataTrade>(
            "/v2/trades", page_size, detail::data_max_page_size, resume,
            [&] { return detail::data_trade_query(filter); }, detail::parse_data_trade_page);
    }

    Paginator<DataActivity> DataClient::activity_pager(const std::string &user,
                                                       const DataActivityFilter &filter,
                                                       std::size_t page_size,
                                                       const PageRequest &resume)
    {
        return cursor_pager<DataActivity>(
            "/v2/activity", page_size, detail::data_max_page_size, resume,
            [&] { return detail::data_activity_query(user, filter); },
            detail::parse_data_activity_page);
    }
} // namespace polymarket
