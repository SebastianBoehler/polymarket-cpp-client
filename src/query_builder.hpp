#pragma once

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace polymarket::detail
{
    // Builds a percent-encoded query string. Unset optionals, empty strings
    // and empty lists are skipped; booleans are sent as true or false.
    class QueryBuilder
    {
      public:
        void add(std::string_view key, const std::optional<bool> &value);
        void add(std::string_view key, const std::optional<double> &value);
        void add(std::string_view key, const std::optional<std::int64_t> &value);
        void add(std::string_view key, const std::string &value);
        // One key=value pair per item.
        void add(std::string_view key, const std::vector<std::string> &values);
        void add(std::string_view key, const std::vector<std::int64_t> &values);
        // One key with the items comma-joined.
        void add_joined(std::string_view key, const std::vector<std::string> &values);
        void add_joined(std::string_view key, const std::vector<std::int64_t> &values);
        // "?a=1&b=2", or "" when nothing was added.
        std::string str() const;

      private:
        void append(std::string_view key, std::string_view value);
        std::string query_;
    };

    // Accepts "" or one of `choices`; throws std::invalid_argument otherwise.
    void check_choice(const char *name, const std::string &value,
                      std::initializer_list<std::string_view> choices);
} // namespace polymarket::detail
