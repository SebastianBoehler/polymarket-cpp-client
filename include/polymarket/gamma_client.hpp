#pragma once

#include "polymarket/environment.hpp"
#include "polymarket/gamma_types.hpp"
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
    // Filters for GammaClient::markets_pager, matching the official SDK's
    // list_markets. Unset optionals, empty strings and empty lists are not
    // sent. Dates are ISO 8601 text. `order` is a comma-separated field list;
    // "volume" and "liquidity" are sent as volumeNum and liquidityNum so they
    // sort numerically. `tag_match` is "any" or "all".
    struct GammaMarketFilter
    {
        std::optional<bool> ascending;
        std::optional<bool> closed;
        std::vector<std::string> clob_token_ids;
        std::vector<std::string> condition_ids;
        std::optional<bool> cyom;
        std::optional<bool> decimalized;
        std::string end_date_max;
        std::string end_date_min;
        std::string game_id;
        std::vector<std::int64_t> ids;
        std::optional<bool> include_tag;
        std::optional<double> liquidity_num_max;
        std::optional<double> liquidity_num_min;
        std::string locale;
        std::vector<std::string> market_maker_addresses;
        std::string order;
        std::vector<std::string> position_ids;
        std::vector<std::string> question_ids;
        std::optional<bool> related_tags;
        std::optional<bool> rfq_enabled;
        std::optional<double> rewards_min_size;
        std::vector<std::string> slugs;
        std::vector<std::string> sports_market_types;
        std::string start_date_max;
        std::string start_date_min;
        std::optional<std::int64_t> tag_id;
        std::string tag_match;
        std::string uma_resolution_status;
        std::optional<double> volume_num_max;
        std::optional<double> volume_num_min;
    };

    // Filters for GammaClient::events_pager, matching the official SDK's
    // list_events, which lists open events unless `closed` is changed.
    // `event_date` is a YYYY-MM-DD date and `recurrence` is "daily",
    // "weekly" or "monthly".
    struct GammaEventFilter
    {
        std::optional<bool> ascending;
        std::optional<bool> closed = false;
        std::optional<bool> cyom;
        std::string end_date_max;
        std::string end_date_min;
        std::optional<bool> ended;
        std::string event_date;
        std::optional<std::int64_t> event_week;
        std::vector<std::int64_t> exclude_tag_ids;
        std::optional<bool> featured;
        std::optional<bool> featured_order;
        std::vector<std::int64_t> game_ids;
        std::vector<std::int64_t> ids;
        std::optional<bool> include_best_lines;
        std::optional<bool> include_chat;
        std::optional<bool> include_children;
        std::optional<bool> include_template;
        std::optional<double> liquidity_max;
        std::optional<double> liquidity_min;
        std::optional<bool> live;
        std::string locale;
        std::string order;
        std::optional<std::int64_t> parent_event_id;
        std::string partner_slug;
        std::string recurrence;
        std::optional<bool> related_tags;
        std::vector<std::int64_t> series_ids;
        std::vector<std::string> slugs;
        std::string start_date_max;
        std::string start_date_min;
        std::string start_time_max;
        std::string start_time_min;
        std::vector<std::int64_t> tag_ids;
        std::string tag_match;
        std::string tag_slug;
        std::string title_search;
        std::optional<double> volume_max;
        std::optional<double> volume_min;
    };

    struct GammaMarketLookupOptions
    {
        std::optional<bool> include_tag;
        std::string locale;
    };

    struct GammaEventLookupOptions
    {
        std::optional<bool> include_best_lines;
        std::optional<bool> include_chat;
        std::optional<bool> include_template;
        std::string locale;
    };

    // Typed client for the Gamma discovery API (Environment::gamma_url). Calls
    // may run from several threads; requests on one client are serialized.
    // Invalid arguments fail with InvalidArgument before any request, and a
    // missing object fails with ApiResponse and http_status 404. Pagers
    // borrow this client, which must outlive them.
    class GammaClient
    {
      public:
        explicit GammaClient(Environment environment = Environment::production(),
                             const std::optional<HttpClientOptions> &http_options = std::nullopt);

        GammaClient(const GammaClient &) = delete;
        GammaClient &operator=(const GammaClient &) = delete;

        Result<GammaMarket> get_market_by_id(const std::string &id,
                                             const GammaMarketLookupOptions &options = {});
        Result<GammaMarket> get_market_by_slug(const std::string &slug,
                                               const GammaMarketLookupOptions &options = {});
        // https://polymarket.com/market/<slug> or /event/<event>[/<market>].
        Result<GammaMarket> get_market_by_url(const std::string &url,
                                              const GammaMarketLookupOptions &options = {});
        Result<std::vector<GammaTagReference>> get_market_tags(const std::string &market_id);

        Result<GammaEvent> get_event_by_id(const std::string &id,
                                           const GammaEventLookupOptions &options = {});
        Result<GammaEvent> get_event_by_slug(const std::string &slug,
                                             const GammaEventLookupOptions &options = {});
        // https://polymarket.com/event/<slug>.
        Result<GammaEvent> get_event_by_url(const std::string &url,
                                            const GammaEventLookupOptions &options = {});
        Result<std::vector<GammaTagReference>> get_event_tags(const std::string &event_id);

        // Keyset pagers over /markets/keyset and /events/keyset. Lists skip
        // markets that are not binary. Pass a resume_token() to continue.
        Paginator<GammaMarket> markets_pager(const GammaMarketFilter &filter = {},
                                             std::size_t page_size = 100,
                                             const PageRequest &resume = {});
        Paginator<GammaEvent> events_pager(const GammaEventFilter &filter = {},
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
        template <typename T, typename BuildPath, typename Parse>
        Result<T> fetch(BuildPath build_path, Parse parse);
    };

} // namespace polymarket
