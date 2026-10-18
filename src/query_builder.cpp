#include "query_builder.hpp"

#include "query_encoding.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <stdexcept>
#include <string>

namespace polymarket::detail
{
    void check_choice(const char *name, const std::string &value,
                      std::initializer_list<std::string_view> choices)
    {
        if (value.empty()) return;
        for (const auto choice : choices)
        {
            if (value == choice) return;
        }
        std::string message = std::string(name) + " must be one of:";
        for (const auto choice : choices)
            message += std::string(" ") + std::string(choice);
        throw std::invalid_argument(message);
    }

    void QueryBuilder::append(std::string_view key, std::string_view value)
    {
        query_ += query_.empty() ? '?' : '&';
        query_ += key;
        query_ += '=';
        query_ += percent_encode_query_value(value);
    }

    void QueryBuilder::add(std::string_view key, const std::optional<bool> &value)
    {
        if (value) append(key, *value ? "true" : "false");
    }

    void QueryBuilder::add(std::string_view key, const std::optional<double> &value)
    {
        if (!value) return;
        if (!std::isfinite(*value))
            throw std::invalid_argument(std::string(key) + " must be finite");
        append(key, nlohmann::json(*value).dump());
    }

    void QueryBuilder::add(std::string_view key, const std::optional<std::int64_t> &value)
    {
        if (value) append(key, std::to_string(*value));
    }

    void QueryBuilder::add(std::string_view key, const std::string &value)
    {
        if (!value.empty()) append(key, value);
    }

    void QueryBuilder::add(std::string_view key, const std::vector<std::string> &values)
    {
        for (const auto &value : values)
            append(key, value);
    }

    void QueryBuilder::add(std::string_view key, const std::vector<std::int64_t> &values)
    {
        for (const auto value : values)
            append(key, std::to_string(value));
    }

    std::string QueryBuilder::str() const
    {
        return query_;
    }

    void QueryBuilder::add_joined(std::string_view key, const std::vector<std::string> &values)
    {
        if (values.empty()) return;
        std::string joined;
        for (const auto &value : values)
        {
            if (!joined.empty()) joined += ',';
            joined += value;
        }
        append(key, joined);
    }

    void QueryBuilder::add_joined(std::string_view key, const std::vector<std::int64_t> &values)
    {
        std::vector<std::string> text;
        text.reserve(values.size());
        for (const auto value : values)
            text.push_back(std::to_string(value));
        add_joined(key, text);
    }
} // namespace polymarket::detail
