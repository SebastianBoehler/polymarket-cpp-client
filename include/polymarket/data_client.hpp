#pragma once

#include "polymarket/data_analytics_types.hpp"
#include "polymarket/data_leaderboard_types.hpp"
#include "polymarket/data_portfolio_types.hpp"
#include "polymarket/data_requests.hpp"
#include "polymarket/data_types.hpp"
#include "polymarket/environment.hpp"
#include "polymarket/http_client.hpp"
#include "polymarket/pagination.hpp"
#include "polymarket/rate_limit.hpp"
#include "polymarket/sdk_error.hpp"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace polymarket
{
    // Typed client for the Data API v2 (Environment::data_url). Calls may run
    // from several threads; requests on one client are serialized. Invalid
    // arguments fail with InvalidArgument before any request. Pagers page by
    // server cursor, at most 1000 rows per page unless noted, and borrow this
    // client, which must outlive them.
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

        Paginator<DataPosition> positions_pager(const DataPositionFilter &filter,
                                                std::size_t page_size = 100,
                                                const PageRequest &resume = {});
        // Condition IDs follow DataPositionFilter's rules.
        Result<DataPortfolioValue>
        get_portfolio_value(const std::string &user,
                            const std::vector<std::string> &condition_ids = {});
        // std::nullopt when the Data API has no stats for the wallet.
        Result<std::optional<DataUserStats>> get_user_stats(const std::string &user);
        // `interval` and `fidelity` are the DataPnlSeries values, or "" for the
        // server default.
        Result<DataPnlSeries> get_user_pnl(const std::string &user,
                                           const std::string &interval = "",
                                           const std::string &fidelity = "");
        Result<DataUserVolume> get_user_volume(const std::string &user,
                                               const DataTimeRange &range = {});
        // The wallet's accounting snapshot as zip archive bytes.
        Result<std::string> download_accounting_snapshot(const std::string &user);

        // Holders of the markets' outcomes. `condition_ids` is required and
        // follows DataPositionFilter's rules; `page_size` applies per outcome.
        Paginator<DataAssetHolders> holders_pager(const std::vector<std::string> &condition_ids,
                                                  const DataHolderFilter &filter = {},
                                                  std::size_t page_size = 100,
                                                  const PageRequest &resume = {});
        // Global open interest when `condition_ids` is empty.
        Result<std::vector<DataOpenInterest>>
        get_open_interests(const std::vector<std::string> &condition_ids = {});
        // Combined taker volume of the events, which are required.
        Result<DataLiveVolume> get_event_live_volume(const std::vector<std::int64_t> &event_ids);
        // Prices oldest first, at most 10000 per page.
        Paginator<DataPricePoint> price_history_pager(const std::string &asset_id,
                                                      const DataPriceHistoryFilter &filter,
                                                      std::size_t page_size = 10000,
                                                      const PageRequest &resume = {});
        // Rows the Data API does not know are left out.
        Result<std::vector<DataResolution>> get_resolutions(const DataResolutionFilter &filter);

        Paginator<DataTraderEntry>
        trader_leaderboard_pager(const DataLeaderboardFilter &filter = {},
                                 std::size_t page_size = 100, const PageRequest &resume = {});
        // std::nullopt when the trader is not on the leaderboard.
        Result<std::optional<DataTraderStanding>>
        get_trader_standing(const std::string &user, const DataLeaderboardFilter &filter = {});
        Paginator<DataBiggestWinner> biggest_winners_pager(const DataLeaderboardFilter &filter = {},
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
        template <typename T, typename BuildPath, typename Parse>
        Result<T> fetch(BuildPath build_path, Parse parse);
    };

} // namespace polymarket
