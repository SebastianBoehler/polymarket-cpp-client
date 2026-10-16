#pragma once

#include "polymarket/gamma_types.hpp"
#include "polymarket/pagination.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Gamma response parsing. Malformed input throws std::exception.
namespace polymarket::detail
{
    using json = nlohmann::json;

    // Absent and null fields read as empty or unset.
    std::string gamma_string(const json &object, const char *key);
    std::string gamma_id(const json &object, const char *key); // string or integer
    std::string gamma_required_id(const json &object, const char *key);
    std::string gamma_hex(const json &object, const char *key); // "" or 0x-prefixed hex
    std::string gamma_decimal_text(const json &object, const char *key);
    std::optional<double> gamma_number(const json &object, const char *key); // "" is unset
    std::optional<std::int64_t> gamma_integer(const json &object, const char *key);
    std::optional<bool> gamma_bool(const json &object, const char *key);
    // A JSON array or a JSON-encoded array string.
    std::vector<json> gamma_sequence(const json &object, const char *key);
    std::vector<std::string> gamma_string_list(const json &object, const char *key);
    std::vector<json> gamma_objects(const json &object, const char *key);

    GammaTagReference parse_gamma_tag_reference(const json &item);
    // Nullopt for a market that does not have exactly two outcomes.
    std::optional<GammaMarket> parse_gamma_binary_market(const json &item);
    GammaEvent parse_gamma_event(const json &item);

    GammaMarket parse_gamma_market_response(const std::string &body);
    GammaEvent parse_gamma_event_response(const std::string &body);
    std::vector<GammaTagReference> parse_gamma_tag_references(const std::string &body);
    // `{items_key: [...], next_cursor?}`; a missing or null cursor ends paging.
    Page<GammaMarket> parse_gamma_market_keyset_page(const std::string &body);
    Page<GammaEvent> parse_gamma_event_keyset_page(const std::string &body);
} // namespace polymarket::detail
