#include "gamma_parsing.hpp"

#include "rest_numeric.hpp"

#include <cctype>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace polymarket::detail
{
    namespace
    {
        const json *field(const json &object, const char *key)
        {
            if (!object.is_object()) throw std::invalid_argument("Gamma value must be an object");
            const auto found = object.find(key);
            if (found == object.end() || found->is_null()) return nullptr;
            return &*found;
        }

        [[noreturn]] void bad_field(const char *key, const char *expected)
        {
            throw std::invalid_argument(std::string("Gamma field ") + key + " must be " + expected);
        }

        template <typename T, typename Parse>
        Page<T> parse_keyset_page(const std::string &body, const char *items_key, Parse parse_item)
        {
            const auto parsed = json::parse(body);
            if (!parsed.is_object())
                throw std::invalid_argument("Gamma keyset page must be an object");
            const auto items = parsed.find(items_key);
            if (items == parsed.end() || !items->is_array()) bad_field(items_key, "an array");
            Page<T> page;
            page.items.reserve(items->size());
            for (const auto &item : *items)
            {
                if (auto value = parse_item(item)) page.items.push_back(std::move(*value));
            }
            if (const auto *cursor = field(parsed, "next_cursor"))
            {
                if (!cursor->is_string() || cursor->get_ref<const std::string &>().empty())
                    bad_field("next_cursor", "a non-empty string when present");
                page.next_cursor = cursor->get<std::string>();
            }
            return page;
        }
    } // namespace

    std::string gamma_string(const json &object, const char *key)
    {
        const auto *value = field(object, key);
        if (value == nullptr) return {};
        if (!value->is_string()) bad_field(key, "a string");
        return value->get<std::string>();
    }

    std::string gamma_required_string(const json &object, const char *key)
    {
        const auto *value = field(object, key);
        if (value == nullptr || !value->is_string()) bad_field(key, "a string");
        return value->get<std::string>();
    }

    std::string gamma_id(const json &object, const char *key)
    {
        const auto *value = field(object, key);
        if (value == nullptr) return {};
        if (value->is_string()) return value->get<std::string>();
        if (value->is_number_unsigned()) return std::to_string(value->get<std::uint64_t>());
        if (value->is_number_integer()) return std::to_string(value->get<std::int64_t>());
        bad_field(key, "a string or integer ID");
    }

    std::string gamma_required_id(const json &object, const char *key)
    {
        auto id = gamma_id(object, key);
        if (id.empty()) bad_field(key, "a non-empty ID");
        return id;
    }

    std::string gamma_hex(const json &object, const char *key)
    {
        auto text = gamma_string(object, key);
        if (text.empty()) return text;
        const bool hex = text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X') &&
                         text.find_first_not_of("0123456789abcdefABCDEF", 2) == std::string::npos;
        if (!hex) bad_field(key, "a 0x-prefixed hex string");
        return text;
    }

    std::string gamma_decimal_text(const json &object, const char *key)
    {
        const auto *value = field(object, key);
        if (value == nullptr) return {};
        if (value->is_number()) return value->dump();
        if (value->is_string())
        {
            const auto &text = value->get_ref<const std::string &>();
            if (text.empty() || is_decimal_number(text)) return text;
        }
        bad_field(key, "a decimal number");
    }

    std::optional<double> gamma_number(const json &object, const char *key)
    {
        const auto *value = field(object, key);
        if (value == nullptr) return std::nullopt;
        if (value->is_string() && value->get_ref<const std::string &>().empty())
            return std::nullopt;
        if (value->is_boolean()) bad_field(key, "a number");
        return strict_json_number(*value);
    }

    std::optional<std::int64_t> gamma_integer(const json &object, const char *key)
    {
        const auto *value = field(object, key);
        if (value == nullptr) return std::nullopt;
        if (value->is_number_unsigned())
        {
            const auto number = value->get<std::uint64_t>();
            if (number > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
                bad_field(key, "a 64-bit integer");
            return static_cast<std::int64_t>(number);
        }
        if (!value->is_number_integer()) bad_field(key, "an integer");
        return value->get<std::int64_t>();
    }

    std::optional<bool> gamma_bool(const json &object, const char *key)
    {
        const auto *value = field(object, key);
        if (value == nullptr) return std::nullopt;
        if (!value->is_boolean()) bad_field(key, "a boolean");
        return value->get<bool>();
    }

    std::vector<json> gamma_sequence(const json &object, const char *key)
    {
        const auto *value = field(object, key);
        if (value == nullptr) return {};
        json parsed;
        const json *list = value;
        if (value->is_string())
        {
            parsed = json::parse(value->get_ref<const std::string &>());
            list = &parsed;
        }
        if (!list->is_array()) bad_field(key, "an array or a JSON-encoded array");
        return std::vector<json>(list->begin(), list->end());
    }

    std::vector<std::string> gamma_string_list(const json &object, const char *key)
    {
        std::vector<std::string> strings;
        for (auto &item : gamma_sequence(object, key))
        {
            if (!item.is_string()) bad_field(key, "a list of strings");
            strings.push_back(item.get<std::string>());
        }
        return strings;
    }

    std::vector<json> gamma_objects(const json &object, const char *key)
    {
        auto items = gamma_sequence(object, key);
        for (const auto &item : items)
        {
            if (!item.is_object()) bad_field(key, "a list of objects");
        }
        return items;
    }

    GammaTagReference parse_gamma_tag_reference(const json &item)
    {
        GammaTagReference tag;
        tag.id = gamma_required_id(item, "id");
        tag.slug = gamma_string(item, "slug");
        tag.label = gamma_string(item, "label");
        tag.force_show = gamma_bool(item, "forceShow");
        tag.force_hide = gamma_bool(item, "forceHide");
        tag.is_carousel = gamma_bool(item, "isCarousel");
        tag.requires_translation = gamma_bool(item, "requiresTranslation");
        tag.created_by = gamma_integer(item, "createdBy");
        tag.updated_by = gamma_integer(item, "updatedBy");
        tag.active_events_count = gamma_integer(item, "activeEventsCount");
        tag.published_at = gamma_string(item, "publishedAt");
        tag.created_at = gamma_string(item, "createdAt");
        tag.updated_at = gamma_string(item, "updatedAt");
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
} // namespace polymarket::detail
