#include "polymarket/gamma_client.hpp"

#include "gamma_client_internal.hpp"
#include "gamma_parsing.hpp"
#include "gamma_query.hpp"

#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

// Parameter names, order and defaults follow py-sdk _internal/actions/gamma.py
// and clients/public.py.
namespace polymarket
{
    namespace
    {
        constexpr std::size_t comments_max_page_size = 100;
        constexpr std::size_t comments_max_offset = 200;

        void require_nonempty(const char *name, const std::string &value)
        {
            if (value.empty()) throw std::invalid_argument(std::string(name) + " cannot be empty");
        }

        std::string search_query(const std::string &q, const GammaSearchOptions &options)
        {
            require_nonempty("q", q);
            detail::gamma_check_choice("recurrence", options.recurrence,
                                       {"daily", "weekly", "monthly"});
            detail::gamma_check_choice("sort", options.sort,
                                       {"volume", "volume_24hr", "liquidity", "competitive",
                                        "closed_time", "start_date", "end_date"});
            detail::GammaQuery query;
            query.add("q", q);
            query.add("ascending", options.ascending);
            query.add("cache", options.cache);
            query.add("events_status", options.events_status);
            query.add("events_tag", options.events_tags);
            query.add("exclude_tag_id", options.exclude_tag_ids);
            query.add("keep_closed_markets", options.keep_closed_markets);
            query.add("optimized", options.optimized);
            query.add("presets", options.presets);
            query.add("recurrence", options.recurrence);
            query.add("search_profiles", options.search_profiles);
            query.add("search_tags", options.search_tags);
            query.add("sort", options.sort);
            return query.str();
        }

        void check_comment_parent(const std::string &parent_entity_id,
                                  const std::string &parent_entity_type)
        {
            require_nonempty("parent_entity_id", parent_entity_id);
            if (parent_entity_type != "Event" && parent_entity_type != "Series")
                throw std::invalid_argument("parent_entity_type must be one of: Event, Series");
        }

        // Holder filtering, positions and other orders are served on offset pages only.
        bool comments_by_cursor(const GammaCommentFilter &filter)
        {
            return !filter.get_positions.value_or(false) && !filter.holders_only.value_or(false) &&
                   (filter.order.empty() || filter.order == "id" || filter.order == "createdAt");
        }
    } // namespace

    Paginator<GammaSearchResults> GammaClient::search_pager(const std::string &query,
                                                            const GammaSearchOptions &options,
                                                            std::size_t page_size,
                                                            const PageRequest &resume)
    {
        constexpr const char *endpoint = "/public-search";
        std::string base;
        std::optional<SdkError> invalid;
        try
        {
            if (page_size == 0) throw std::invalid_argument("page_size must be positive");
            base = search_query(query, options);
        }
        catch (const std::invalid_argument &ex)
        {
            invalid = detail::gamma_invalid_argument(ex.what(), endpoint);
        }
        if (invalid)
        {
            return Paginator<GammaSearchResults>::numbered(
                endpoint, [error = std::move(*invalid)](const PageRequest &)
                { return Result<Page<GammaSearchResults>>::failure(error); }, 1, resume);
        }
        return Paginator<GammaSearchResults>::numbered(
            endpoint,
            [this, prefix = std::string(endpoint) + base + "&page="](const PageRequest &request)
            {
                const auto path = prefix + std::to_string(request.page) +
                                  "&limit_per_type=" + std::to_string(request.limit);
                return detail::gamma_page<GammaSearchResults>(get(path), endpoint,
                                                              detail::parse_gamma_search_page);
            },
            page_size, resume);
    }

    Result<std::vector<GammaComment>>
    GammaClient::get_comment_thread(const std::string &id, std::optional<bool> get_positions)
    {
        return fetch<std::vector<GammaComment>>(
            [&]
            {
                require_nonempty("id", id);
                detail::GammaQuery query;
                query.add("get_positions", get_positions);
                return "/comments/" + detail::percent_encode_query_value(id) + query.str();
            },
            detail::parse_gamma_comments);
    }

    Paginator<GammaComment> GammaClient::comments_pager(const std::string &parent_entity_id,
                                                        const std::string &parent_entity_type,
                                                        const GammaCommentFilter &filter,
                                                        std::size_t page_size,
                                                        const PageRequest &resume)
    {
        if (!comments_by_cursor(filter))
        {
            return offset_pager<GammaComment>(
                "/comments", page_size, comments_max_page_size, comments_max_offset, resume,
                [&]
                {
                    check_comment_parent(parent_entity_id, parent_entity_type);
                    detail::GammaQuery query;
                    query.add("parent_entity_id", parent_entity_id);
                    query.add("parent_entity_type", parent_entity_type);
                    query.add("ascending", filter.ascending);
                    query.add("get_positions", filter.get_positions);
                    query.add("holders_only", filter.holders_only);
                    query.add("order", filter.order);
                    return query.str();
                },
                detail::parse_gamma_comment_root_page);
        }

        constexpr const char *endpoint = "/comments/keyset";
        std::string query;
        std::optional<SdkError> invalid;
        try
        {
            check_comment_parent(parent_entity_id, parent_entity_type);
            if (page_size == 0 || page_size > comments_max_page_size)
                throw std::invalid_argument("page_size must be between 1 and 100");
            // Pinned so the first page matches the offset listing's newest-first default.
            const std::string order = filter.order.empty() ? "createdAt" : filter.order;
            const bool ascending = filter.order.empty() ? false : filter.ascending.value_or(true);
            detail::GammaQuery builder;
            builder.add("parent_entity_id", parent_entity_id);
            builder.add("parent_entity_type", parent_entity_type);
            builder.add("order", order);
            builder.add("ascending", std::optional<bool>(ascending));
            query = builder.str();
        }
        catch (const std::invalid_argument &ex)
        {
            invalid = detail::gamma_invalid_argument(ex.what(), endpoint);
        }
        return detail::gamma_keyset_pager<GammaComment>(
            endpoint, query, page_size, resume, std::move(invalid), [this](const std::string &path)
            { return get(path); }, detail::parse_gamma_comment_keyset_page);
    }

    Paginator<GammaComment> GammaClient::user_comments_pager(const std::string &address,
                                                             const GammaUserCommentFilter &filter,
                                                             std::size_t page_size,
                                                             const PageRequest &resume)
    {
        const std::string endpoint =
            "/comments/user_address/" + detail::percent_encode_query_value(address);
        return offset_pager<GammaComment>(
            endpoint, page_size, comments_max_page_size, comments_max_offset, resume,
            [&]
            {
                require_nonempty("address", address);
                detail::GammaQuery query;
                query.add("ascending", filter.ascending);
                query.add("order", filter.order);
                return query.str();
            },
            detail::gamma_list_page<GammaComment>(detail::parse_gamma_comments));
    }

    Result<std::optional<GammaPublicProfile>>
    GammaClient::get_public_profile(const std::string &address)
    {
        auto result = fetch<GammaPublicProfile>(
            [&]
            {
                require_nonempty("address", address);
                detail::GammaQuery query;
                query.add("address", address);
                return "/public-profile" + query.str();
            },
            detail::parse_gamma_public_profile);
        if (result.ok())
            return Result<std::optional<GammaPublicProfile>>::success(std::move(result.value()));
        if (result.error().code == SdkErrorCode::ApiResponse && result.error().http_status == 404)
            return Result<std::optional<GammaPublicProfile>>::success(std::nullopt);
        return Result<std::optional<GammaPublicProfile>>::failure(result.error());
    }
} // namespace polymarket
