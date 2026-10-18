#include "gamma_parsing.hpp"


#include <cctype>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace polymarket::detail
{
    namespace
    {
        template <typename T, typename Parse>
        Page<T> parse_keyset_page(const std::string &body, const char *items_key, Parse parse_item)
        {
            const auto parsed = json::parse(body);
            if (!parsed.is_object())
                throw std::invalid_argument("Gamma keyset page must be an object");
            const auto items = parsed.find(items_key);
            if (items == parsed.end() || !items->is_array()) throw_bad_field(items_key, "an array");
            Page<T> page;
            page.items.reserve(items->size());
            for (const auto &item : *items)
            {
                if (auto value = parse_item(item)) page.items.push_back(std::move(*value));
            }
            if (const auto *cursor = find_field(parsed, "next_cursor"))
            {
                if (!cursor->is_string() || cursor->get_ref<const std::string &>().empty())
                    throw_bad_field("next_cursor", "a non-empty string when present");
                page.next_cursor = cursor->get<std::string>();
            }
            return page;
        }
    } // namespace


    GammaTagReference parse_gamma_tag_reference(const json &item)
    {
        GammaTagReference tag;
        tag.id = field_required_id(item, "id");
        tag.slug = field_string(item, "slug");
        tag.label = field_string(item, "label");
        tag.force_show = field_bool(item, "forceShow");
        tag.force_hide = field_bool(item, "forceHide");
        tag.is_carousel = field_bool(item, "isCarousel");
        tag.requires_translation = field_bool(item, "requiresTranslation");
        tag.created_by = field_integer(item, "createdBy");
        tag.updated_by = field_integer(item, "updatedBy");
        tag.active_events_count = field_integer(item, "activeEventsCount");
        tag.published_at = field_string(item, "publishedAt");
        tag.created_at = field_string(item, "createdAt");
        tag.updated_at = field_string(item, "updatedAt");
        tag.raw = item;
        return tag;
    }

    std::vector<GammaTagReference> parse_gamma_tag_references(const std::string &body)
    {
        const auto parsed = json::parse(body);
        if (!parsed.is_array()) throw std::invalid_argument("Gamma tags response must be an array");
        std::vector<GammaTagReference> tags;
        tags.reserve(parsed.size());
        for (const auto &item : parsed)
            tags.push_back(parse_gamma_tag_reference(item));
        return tags;
    }

    GammaMarket parse_gamma_market_response(const std::string &body)
    {
        auto market = parse_gamma_binary_market(json::parse(body));
        if (!market) throw std::invalid_argument("Gamma market must have exactly two outcomes");
        return std::move(*market);
    }

    GammaEvent parse_gamma_event_response(const std::string &body)
    {
        return parse_gamma_event(json::parse(body));
    }

    Page<GammaMarket> parse_gamma_market_keyset_page(const std::string &body)
    {
        return parse_keyset_page<GammaMarket>(body, "markets", parse_gamma_binary_market);
    }

    Page<GammaEvent> parse_gamma_event_keyset_page(const std::string &body)
    {
        return parse_keyset_page<GammaEvent>(
            body, "events",
            [](const json &item) { return std::optional<GammaEvent>(parse_gamma_event(item)); });
    }

    Page<GammaComment> parse_gamma_comment_keyset_page(const std::string &body)
    {
        return parse_keyset_page<GammaComment>(
            body, "comments", [](const json &item)
            { return std::optional<GammaComment>(parse_gamma_comment(item)); });
    }
} // namespace polymarket::detail
