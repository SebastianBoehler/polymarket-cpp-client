#pragma once

#include "polymarket/data_requests.hpp"
#include "polymarket/data_types.hpp"
#include "polymarket/environment.hpp"
#include "polymarket/http_client.hpp"
#include "polymarket/pagination.hpp"
#include "polymarket/rate_limit.hpp"
#include "polymarket/sdk_error.hpp"

#include <cstddef>
#include <mutex>
#include <optional>
#include <string>

namespace polymarket
{
    // Typed client for the Data API v2 (Environment::data_url). Calls may run
    // from several threads; requests on one client are serialized. Invalid
    // arguments fail with InvalidArgument before any request. Pagers page by
    // server cursor, at most 1000 rows per page, and borrow this client, which
    // must outlive them.
    class DataClient
    {
      public:
        explicit DataClient(Environment environment = Environment::production(),
                            const std::optional<HttpClientOptions> &http_options = std::nullopt);

        DataClient(const DataClient &) = delete;
        DataClient &operator=(const DataClient &) = delete;

        // Trades across Polymarket, or one wallet's with `filter.user`.
        Paginator<DataTrade> trades_pager(const DataTradeFilter &filter = {},
                                          std::size_t page_size = 100,
                                          const PageRequest &resume = {});
        // A wallet's activity, deposits and withdrawals included.
        Paginator<DataActivity> activity_pager(const std::string &user,
                                               const DataActivityFilter &filter = {},
                                               std::size_t page_size = 100,
                                               const PageRequest &resume = {});

        // Reads that get HTTP 429 are retried after the server's Retry-After
        // delay. Defaults to RateLimitRetry{}; std::nullopt disables retries.
        void set_rate_limit_retry(std::optional<RateLimitRetry> policy);

        const Environment &environment() const { return environment_; }

      private:
        Environment environment_;
        HttpClient http_;
        mutable std::mutex rate_limit_mutex_;
        std::optional<RateLimitRetry> rate_limit_retry_{RateLimitRetry{}};

        HttpResponse get(const std::string &path);
        template <typename T, typename BuildQuery, typename ParsePage>
        Paginator<T> cursor_pager(const char *endpoint, std::size_t page_size,
                                  std::size_t max_page_size, const PageRequest &resume,
                                  BuildQuery build_query, ParsePage parse_page);
    };

} // namespace polymarket
