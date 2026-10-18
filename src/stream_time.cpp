#include "stream_time.hpp"

#include "market_time.hpp"

#include <string>

namespace polymarket::detail
{
    namespace
    {
        constexpr std::int64_t max_epoch_ms = 253402300799999; // 9999-12-31T23:59:59.999Z

        bool all_digits(const std::string &text)
        {
            return !text.empty() && text.find_first_not_of("0123456789") == std::string::npos;
        }

        std::int64_t checked_ms(const char *key, const std::string &digits)
        {
            if (digits.size() > 15) throw_bad_field(key, "Unix milliseconds");
            const auto ms = std::stoll(digits);
            if (ms > max_epoch_ms) throw_bad_field(key, "Unix milliseconds");
            return ms;
        }

        std::int64_t iso_ms(const char *key, const std::string &text)
        {
            std::string utc = text;
            std::int64_t offset_minutes = 0;
            const auto size = text.size();
            if (size > 6 && (text[size - 6] == '+' || text[size - 6] == '-') &&
                text[size - 3] == ':')
            {
                const auto hours = text.substr(size - 5, 2);
                const auto minutes = text.substr(size - 2, 2);
                if (!all_digits(hours) || !all_digits(minutes))
                    throw_bad_field(key, "an ISO 8601 time");
                offset_minutes = std::stoll(hours) * 60 + std::stoll(minutes);
                if (text[size - 6] == '-') offset_minutes = -offset_minutes;
                utc = text.substr(0, size - 6) + "Z";
            }
            else if (text.back() != 'Z')
            {
                utc = text + "Z";
            }
            const auto ms = parse_iso8601_ms(utc);
            if (ms == 0) throw_bad_field(key, "an ISO 8601 time");
            return static_cast<std::int64_t>(ms) - offset_minutes * 60000;
        }
    } // namespace

    std::optional<std::int64_t> stream_epoch_ms_or_iso(const json &object, const char *key)
    {
        const auto *value = find_field(object, key);
        if (value == nullptr) return std::nullopt;
        if (value->is_string())
        {
            const auto &text = value->get_ref<const std::string &>();
            if (text.empty()) return std::nullopt;
            return all_digits(text) ? checked_ms(key, text) : iso_ms(key, text);
        }
        const auto ms = field_integer(object, key);
        if (!ms || *ms < 0 || *ms > max_epoch_ms) throw_bad_field(key, "Unix milliseconds");
        return ms;
    }
} // namespace polymarket::detail
