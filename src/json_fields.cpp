#include "json_fields.hpp"

#include "rest_numeric.hpp"

#include <limits>
#include <stdexcept>
#include <string>

namespace polymarket::detail
{
    const json *find_field(const json &object, const char *key)
    {
        if (!object.is_object()) throw std::invalid_argument("response value must be an object");
        const auto found = object.find(key);
        if (found == object.end() || found->is_null()) return nullptr;
        return &*found;
    }

    void throw_bad_field(const char *key, const char *expected)
    {
        throw std::invalid_argument(std::string("field ") + key + " must be " + expected);
    }

    std::string field_string(const json &object, const char *key)
    {
        const auto *value = find_field(object, key);
        if (value == nullptr) return {};
        if (!value->is_string()) throw_bad_field(key, "a string");
        return value->get<std::string>();
    }

    std::string field_required_string(const json &object, const char *key)
    {
        const auto *value = find_field(object, key);
        if (value == nullptr || !value->is_string()) throw_bad_field(key, "a string");
        return value->get<std::string>();
    }

    std::string field_id(const json &object, const char *key)
    {
        const auto *value = find_field(object, key);
        if (value == nullptr) return {};
        if (value->is_string()) return value->get<std::string>();
        if (value->is_number_unsigned()) return std::to_string(value->get<std::uint64_t>());
        if (value->is_number_integer()) return std::to_string(value->get<std::int64_t>());
        throw_bad_field(key, "a string or integer ID");
    }

    std::string field_required_id(const json &object, const char *key)
    {
        auto id = field_id(object, key);
        if (id.empty()) throw_bad_field(key, "a non-empty ID");
        return id;
    }

    std::string field_hex(const json &object, const char *key)
    {
        auto text = field_string(object, key);
        if (text.empty()) return text;
        const bool hex = text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X') &&
                         text.find_first_not_of("0123456789abcdefABCDEF", 2) == std::string::npos;
        if (!hex) throw_bad_field(key, "a 0x-prefixed hex string");
        return text;
    }

    std::string field_decimal_text(const json &object, const char *key)
    {
        const auto *value = find_field(object, key);
        if (value == nullptr) return {};
        if (value->is_number()) return value->dump();
        if (value->is_string())
        {
            const auto &text = value->get_ref<const std::string &>();
            if (text.empty() || is_decimal_number(text)) return text;
        }
        throw_bad_field(key, "a decimal number");
    }

    std::optional<double> field_number(const json &object, const char *key)
    {
        const auto *value = find_field(object, key);
        if (value == nullptr) return std::nullopt;
        if (value->is_string() && value->get_ref<const std::string &>().empty())
            return std::nullopt;
        if (value->is_boolean()) throw_bad_field(key, "a number");
        return strict_json_number(*value);
    }

    std::optional<std::int64_t> field_integer(const json &object, const char *key)
    {
        const auto *value = find_field(object, key);
        if (value == nullptr) return std::nullopt;
        if (value->is_number_unsigned())
        {
            const auto number = value->get<std::uint64_t>();
            if (number > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
                throw_bad_field(key, "a 64-bit integer");
            return static_cast<std::int64_t>(number);
        }
        if (!value->is_number_integer()) throw_bad_field(key, "an integer");
        return value->get<std::int64_t>();
    }

    std::optional<bool> field_bool(const json &object, const char *key)
    {
        const auto *value = find_field(object, key);
        if (value == nullptr) return std::nullopt;
        if (!value->is_boolean()) throw_bad_field(key, "a boolean");
        return value->get<bool>();
    }

    std::vector<json> field_sequence(const json &object, const char *key)
    {
        const auto *value = find_field(object, key);
        if (value == nullptr) return {};
        json parsed;
        const json *list = value;
        if (value->is_string())
        {
            parsed = json::parse(value->get_ref<const std::string &>());
            list = &parsed;
        }
        if (!list->is_array()) throw_bad_field(key, "an array or a JSON-encoded array");
        return std::vector<json>(list->begin(), list->end());
    }

    std::vector<std::string> field_string_list(const json &object, const char *key)
    {
        std::vector<std::string> strings;
        for (auto &item : field_sequence(object, key))
        {
            if (!item.is_string()) throw_bad_field(key, "a list of strings");
            strings.push_back(item.get<std::string>());
        }
        return strings;
    }

    std::vector<json> field_objects(const json &object, const char *key)
    {
        auto items = field_sequence(object, key);
        for (const auto &item : items)
        {
            if (!item.is_object()) throw_bad_field(key, "a list of objects");
        }
        return items;
    }
} // namespace polymarket::detail
