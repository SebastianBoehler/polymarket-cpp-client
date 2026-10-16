#pragma once

#include "polymarket/environment.hpp"
#include "polymarket/gamma_catalog_types.hpp"
#include "polymarket/gamma_requests.hpp"
#include "polymarket/gamma_social_types.hpp"
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

        Result<GammaTag> get_tag_by_id(const std::string &id,
                                       const GammaTagLookupOptions &options = {});
        Result<GammaTag> get_tag_by_slug(const std::string &slug,
                                         const GammaTagLookupOptions &options = {});
        Result<std::vector<GammaRelatedTag>>
        get_related_tags_by_id(const std::string &id, const GammaRelatedTagOptions &options = {});
        Result<std::vector<GammaRelatedTag>>
        get_related_tags_by_slug(const std::string &slug,
                                 const GammaRelatedTagOptions &options = {});
        // The tags that related-tag links point to.
        Result<std::vector<GammaTag>>
        get_related_tag_resources_by_id(const std::string &id,
                                        const GammaRelatedTagResourceOptions &options = {});
        Result<std::vector<GammaTag>>
        get_related_tag_resources_by_slug(const std::string &slug,
                                          const GammaRelatedTagResourceOptions &options = {});

        Result<GammaSeries> get_series(const std::string &id, const std::string &locale = "");
        Result<std::vector<GammaSport>> get_sports();
        Result<std::vector<std::string>> get_sports_market_types();

        // Offset pagers over /tags, /series and /teams. Gamma caps a page at
        // 100 tags, 50 series or 100 teams; a larger page_size fails with
        // InvalidArgument.
        Paginator<GammaTag> tags_pager(const GammaTagFilter &filter = {},
                                       std::size_t page_size = 20, const PageRequest &resume = {});
        Paginator<GammaSeries> series_pager(const GammaSeriesFilter &filter = {},
                                            std::size_t page_size = 20,
                                            const PageRequest &resume = {});
        Paginator<GammaTeam> teams_pager(const GammaTeamFilter &filter = {},
                                         std::size_t page_size = 20,
                                         const PageRequest &resume = {});

        // One aggregate result per page, from page 1 while the server reports
        // more. page_size is the limit per result type.
        Paginator<GammaSearchResults> search_pager(const std::string &query,
                                                   const GammaSearchOptions &options = {},
                                                   std::size_t page_size = 10,
                                                   const PageRequest &resume = {});

        // A comment and its replies.
        Result<std::vector<GammaComment>>
        get_comment_thread(const std::string &id, std::optional<bool> get_positions = std::nullopt);
        // Comments on an Event or Series (`parent_entity_type`). Plain reads
        // page by server cursor through the whole thread. Reads with
        // get_positions, holders_only or another order use offset pages, where
        // page_size counts top-level comments with their replies riding along,
        // and stop with truncated() at offset 200, the deepest page Gamma serves.
        // At most 100 comments per page.
        Paginator<GammaComment> comments_pager(const std::string &parent_entity_id,
                                               const std::string &parent_entity_type,
                                               const GammaCommentFilter &filter = {},
                                               std::size_t page_size = 20,
                                               const PageRequest &resume = {});
        // Offset pages of at most 100 comments, stopping with truncated() at
        // offset 200; Gamma serves nothing deeper for this listing.
        Paginator<GammaComment> user_comments_pager(const std::string &address,
                                                    const GammaUserCommentFilter &filter = {},
                                                    std::size_t page_size = 20,
                                                    const PageRequest &resume = {});
        // Empty when Gamma has no profile for the address.
        Result<std::optional<GammaPublicProfile>> get_public_profile(const std::string &address);

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
        template <typename T, typename BuildQuery, typename ParsePage>
        Paginator<T> offset_pager(const std::string &endpoint, std::size_t page_size,
                                  std::size_t max_page_size, std::size_t max_offset,
                                  const PageRequest &resume, BuildQuery build_query,
                                  ParsePage parse_page);
    };

} // namespace polymarket
