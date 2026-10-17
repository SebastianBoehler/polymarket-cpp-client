#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Strict readers for fields of REST response objects. Malformed input throws
// std::invalid_argument naming the field.
namespace polymarket::detail
{
    using json = nlohmann::json;

    // Null when the field is absent or null; throws when `object` is not an object.
    const json *find_field(const json &object, const char *key);
    [[noreturn]] void throw_bad_field(const char *key, const char *expected);

    // Absent and null fields read as empty or unset.
    std::string field_string(const json &object, const char *key);
    std::string field_id(const json &object, const char *key); // string or integer
    std::string field_required_id(const json &object, const char *key);
    std::string field_hex(const json &object, const char *key); // "" or 0x-prefixed hex
    std::string field_decimal_text(const json &object, const char *key);
    std::optional<double> field_number(const json &object, const char *key); // "" is unset
    std::optional<std::int64_t> field_integer(const json &object, const char *key);
    std::optional<bool> field_bool(const json &object, const char *key);
    // A JSON array or a JSON-encoded array string.
    std::vector<json> field_sequence(const json &object, const char *key);
    std::vector<std::string> field_string_list(const json &object, const char *key);
    std::vector<json> field_objects(const json &object, const char *key);

    std::string field_required_string(const json &object, const char *key);
} // namespace polymarket::detail
