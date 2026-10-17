#include "gamma_query.hpp"

#include "query_encoding.hpp"

#include <stdexcept>
#include <string>
#include <vector>

// Parameter names and order follow py-sdk _internal/actions/gamma.py.
namespace polymarket::detail
{
    namespace
    {
        std::string trim(std::string_view text)
        {
            const auto first = text.find_first_not_of(" \t");
            if (first == std::string_view::npos) return {};
            const auto last = text.find_last_not_of(" \t");
            return std::string(text.substr(first, last - first + 1));
        }

        // Market volume and liquidity sort as text unless their numeric twins are named.
        std::string normalize_market_order(const std::string &order)
        {
            if (order.empty()) return order;
            std::string normalized;
            std::size_t start = 0;
            while (true)
            {
                const auto comma = order.find(',', start);
                auto token = trim(std::string_view(order).substr(start, comma - start));
                if (token == "volume") token = "volumeNum";
                if (token == "liquidity") token = "liquidityNum";
                if (start != 0) normalized += ',';
                normalized += token;
                if (comma == std::string::npos) break;
                start = comma + 1;
            }
            return normalized;
        }

        void require_nonempty(const char *name, const std::string &value)
        {
            if (value.empty()) throw std::invalid_argument(std::string(name) + " cannot be empty");
        }
    } // namespace


    std::string gamma_market_list_query(const GammaMarketFilter &filter)
    {
        check_choice("tag_match", filter.tag_match, {"any", "all"});
        QueryBuilder query;
        query.add("ascending", filter.ascending);
        query.add("closed", filter.closed);
        query.add("clob_token_ids", filter.clob_token_ids);
        query.add("condition_ids", filter.condition_ids);
        query.add("cyom", filter.cyom);
        query.add("decimalized", filter.decimalized);
        query.add("end_date_max", filter.end_date_max);
        query.add("end_date_min", filter.end_date_min);
        query.add("game_id", filter.game_id);
        query.add("id", filter.ids);
        query.add("include_tag", filter.include_tag);
        query.add("liquidity_num_max", filter.liquidity_num_max);
        query.add("liquidity_num_min", filter.liquidity_num_min);
        query.add("locale", filter.locale);
        query.add("market_maker_address", filter.market_maker_addresses);
        query.add("order", normalize_market_order(filter.order));
        query.add("position_ids", filter.position_ids);
        query.add("question_ids", filter.question_ids);
        query.add("related_tags", filter.related_tags);
        query.add("rfq_enabled", filter.rfq_enabled);
        query.add("rewards_min_size", filter.rewards_min_size);
        query.add("slug", filter.slugs);
        query.add("sports_market_types", filter.sports_market_types);
        query.add("start_date_max", filter.start_date_max);
        query.add("start_date_min", filter.start_date_min);
        query.add("tag_id", filter.tag_id);
        query.add("tag_match", filter.tag_match);
        query.add("uma_resolution_status", filter.uma_resolution_status);
        query.add("volume_num_max", filter.volume_num_max);
        query.add("volume_num_min", filter.volume_num_min);
        return query.str();
    }

    std::string gamma_event_list_query(const GammaEventFilter &filter)
    {
        check_choice("recurrence", filter.recurrence, {"daily", "weekly", "monthly"});
        check_choice("tag_match", filter.tag_match, {"any", "all"});
        QueryBuilder query;
        query.add("ascending", filter.ascending);
        query.add("closed", filter.closed);
        query.add("cyom", filter.cyom);
        query.add("end_date_max", filter.end_date_max);
        query.add("end_date_min", filter.end_date_min);
        query.add("ended", filter.ended);
        query.add("event_date", filter.event_date);
        query.add("event_week", filter.event_week);
        query.add("exclude_tag_id", filter.exclude_tag_ids);
        query.add("featured", filter.featured);
        query.add("featured_order", filter.featured_order);
        query.add("game_id", filter.game_ids);
        query.add("id", filter.ids);
        query.add("include_best_lines", filter.include_best_lines);
        query.add("include_chat", filter.include_chat);
        query.add("include_children", filter.include_children);
        query.add("include_template", filter.include_template);
        query.add("liquidity_max", filter.liquidity_max);
        query.add("liquidity_min", filter.liquidity_min);
        query.add("live", filter.live);
        query.add("locale", filter.locale);
        query.add("order", filter.order);
        query.add("parent_event_id", filter.parent_event_id);
        query.add("partner_slug", filter.partner_slug);
        query.add("recurrence", filter.recurrence);
        query.add("related_tags", filter.related_tags);
        query.add("series_id", filter.series_ids);
        query.add("slug", filter.slugs);
        query.add("start_date_max", filter.start_date_max);
        query.add("start_date_min", filter.start_date_min);
        query.add("start_time_max", filter.start_time_max);
        query.add("start_time_min", filter.start_time_min);
        query.add("tag_id", filter.tag_ids);
        query.add("tag_match", filter.tag_match);
        query.add("tag_slug", filter.tag_slug);
        query.add("title_search", filter.title_search);
        query.add("volume_max", filter.volume_max);
        query.add("volume_min", filter.volume_min);
        return query.str();
    }

    std::string gamma_market_lookup_query(const GammaMarketLookupOptions &options)
    {
        QueryBuilder query;
        query.add("include_tag", options.include_tag);
        query.add("locale", options.locale);
        return query.str();
    }

    std::string gamma_event_lookup_query(const GammaEventLookupOptions &options)
    {
        QueryBuilder query;
        query.add("include_best_lines", options.include_best_lines);
        query.add("include_chat", options.include_chat);
        query.add("include_template", options.include_template);
        query.add("locale", options.locale);
        return query.str();
    }

    std::string gamma_id_path(std::string_view kind, const std::string &id)
    {
        require_nonempty("id", id);
        return "/" + std::string(kind) + "s/" + percent_encode_query_value(id);
    }

    std::string gamma_slug_path(std::string_view kind, const std::string &slug)
    {
        require_nonempty("slug", slug);
        return "/" + std::string(kind) + "s/slug/" + percent_encode_query_value(slug);
    }

    std::string gamma_slug_from_url(std::string_view kind, const std::string &url)
    {
        constexpr std::string_view scheme = "https://";
        const std::string_view text(url);
        if (text.substr(0, scheme.size()) != scheme)
            throw std::invalid_argument("Expected a valid Polymarket URL.");
        const auto rest = text.substr(scheme.size());
        const auto authority_end = rest.find_first_of("/?#");
        const auto authority = rest.substr(0, authority_end);
        if (authority != "polymarket.com" && authority != "www.polymarket.com")
            throw std::invalid_argument("Expected a valid Polymarket URL.");

        std::vector<std::string> segments;
        if (authority_end != std::string_view::npos && rest[authority_end] == '/')
        {
            auto path = rest.substr(authority_end);
            path = path.substr(0, path.find_first_of("?#"));
            std::size_t start = 0;
            while (start <= path.size())
            {
                const auto slash = path.find('/', start);
                const auto segment = path.substr(start, slash - start);
                if (!segment.empty()) segments.emplace_back(segment);
                if (slash == std::string_view::npos) break;
                start = slash + 1;
            }
        }

        if (kind == "market" && segments.size() >= 2 && segments.size() <= 3 &&
            segments[0] == "event")
            return segments.back();
        if (segments.size() != 2 || segments[0] != kind)
            throw std::invalid_argument("Expected a Polymarket " + std::string(kind) + " URL.");
        return segments[1];
    }
} // namespace polymarket::detail
