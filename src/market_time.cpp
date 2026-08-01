#include "market_fetcher.hpp"
#include "market_time.hpp"

#include <algorithm>
#include <ctime>
#include <limits>

namespace polymarket
{
    namespace
    {
        int64_t days_from_civil(int year, unsigned month, unsigned day)
        {
            year -= month <= 2;
            const int era = (year >= 0 ? year : year - 399) / 400;
            const unsigned year_of_era = static_cast<unsigned>(year - era * 400);
            const unsigned day_of_year =
                (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
            const unsigned day_of_era = year_of_era * 365 + year_of_era / 4 -
                                        year_of_era / 100 + day_of_year;
            return era * 146097 + static_cast<int>(day_of_era) - 719468;
        }

        int64_t civil_epoch(int year, unsigned month, unsigned day, unsigned hour = 0,
                            unsigned minute = 0, unsigned second = 0)
        {
            return days_from_civil(year, month, day) * 86400 + hour * 3600 +
                   minute * 60 + second;
        }

        bool leap_year(int year)
        {
            return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
        }

        unsigned days_in_month(int year, unsigned month)
        {
            static constexpr unsigned days[] = {
                0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
            return month == 2 && leap_year(year) ? 29 : days[month];
        }

        bool parse_digits(const std::string &value, std::size_t offset,
                          std::size_t count, unsigned &result)
        {
            if (offset + count > value.size()) return false;
            result = 0;
            for (std::size_t index = offset; index < offset + count; ++index)
            {
                const char digit = value[index];
                if (digit < '0' || digit > '9') return false;
                result = result * 10 + static_cast<unsigned>(digit - '0');
            }
            return true;
        }

        int weekday(int year, unsigned month, unsigned day)
        {
            const auto value = (days_from_civil(year, month, day) + 4) % 7;
            return static_cast<int>(value < 0 ? value + 7 : value);
        }

        int new_york_utc_offset(uint64_t unix_seconds)
        {
            const std::time_t value = static_cast<std::time_t>(unix_seconds);
            std::tm utc{};
            gmtime_r(&value, &utc);
            const int year = utc.tm_year + 1900;
            const unsigned second_sunday = 1 + (7 - weekday(year, 3, 1)) % 7 + 7;
            const unsigned first_sunday = 1 + (7 - weekday(year, 11, 1)) % 7;
            const auto dst_start = civil_epoch(year, 3, second_sunday, 7);
            const auto dst_end = civil_epoch(year, 11, first_sunday, 6);
            return unix_seconds >= static_cast<uint64_t>(dst_start) &&
                           unix_seconds < static_cast<uint64_t>(dst_end)
                       ? -4 * 3600
                       : -5 * 3600;
        }
    }

    namespace detail
    {
        uint64_t parse_iso8601_ms(const std::string &value)
        {
            if (value.size() < 20 || value[4] != '-' || value[7] != '-' ||
                value[10] != 'T' || value[13] != ':' || value[16] != ':' ||
                value.back() != 'Z')
            {
                return 0;
            }
            unsigned year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
            if (!parse_digits(value, 0, 4, year) ||
                !parse_digits(value, 5, 2, month) ||
                !parse_digits(value, 8, 2, day) ||
                !parse_digits(value, 11, 2, hour) ||
                !parse_digits(value, 14, 2, minute) ||
                !parse_digits(value, 17, 2, second) ||
                year < 1970 || month < 1 || month > 12 || day < 1 ||
                day > days_in_month(static_cast<int>(year), month) ||
                hour > 23 || minute > 59 || second > 59)
            {
                return 0;
            }

            unsigned milliseconds = 0;
            if (value[19] == '.')
            {
                const std::size_t fraction_size = value.size() - 21;
                if (fraction_size == 0) return 0;
                for (std::size_t index = 20; index + 1 < value.size(); ++index)
                {
                    if (value[index] < '0' || value[index] > '9') return 0;
                }
                const auto parsed_digits = std::min<std::size_t>(3, fraction_size);
                if (!parse_digits(value, 20, parsed_digits, milliseconds)) return 0;
                if (parsed_digits == 1) milliseconds *= 100;
                if (parsed_digits == 2) milliseconds *= 10;
            }
            else if (value[19] != 'Z' || value.size() != 20)
            {
                return 0;
            }

            const auto epoch = civil_epoch(static_cast<int>(year), month, day,
                                           hour, minute, second);
            if (epoch < 0 || static_cast<uint64_t>(epoch) >
                                 (std::numeric_limits<uint64_t>::max() - milliseconds) / 1000)
            {
                return 0;
            }
            return static_cast<uint64_t>(epoch) * 1000 + milliseconds;
        }

        std::vector<std::string> generate_new_york_hour_slugs(
            const std::string &asset_name, uint64_t now, int count)
        {
            std::vector<std::string> slugs;
            if (count <= 0) return slugs;
            slugs.reserve(static_cast<std::size_t>(count));
            for (int offset = -1; slugs.size() < static_cast<std::size_t>(count); ++offset)
            {
                const auto timestamp = static_cast<int64_t>(now) +
                                       static_cast<int64_t>(offset) * 3600;
                if (timestamp < 0) continue;
                auto slug = format_new_york_hour_slug(
                    asset_name, static_cast<uint64_t>(timestamp));
                if (std::find(slugs.begin(), slugs.end(), slug) == slugs.end())
                {
                    slugs.push_back(std::move(slug));
                }
            }
            return slugs;
        }

        std::string format_new_york_hour_slug(const std::string &asset_name,
                                              uint64_t unix_seconds)
        {
            static const std::vector<std::string> months = {
                "january", "february", "march", "april", "may", "june",
                "july", "august", "september", "october", "november", "december"};
            const std::time_t local_value = static_cast<std::time_t>(
                static_cast<int64_t>(unix_seconds) + new_york_utc_offset(unix_seconds));
            std::tm local{};
            gmtime_r(&local_value, &local);
            const int hour = local.tm_hour;
            const std::string clock = hour == 0   ? "12am"
                                      : hour == 12 ? "12pm"
                                      : hour < 12  ? std::to_string(hour) + "am"
                                                   : std::to_string(hour - 12) + "pm";
            return asset_name + "-up-or-down-" + months[local.tm_mon] + "-" +
                   std::to_string(local.tm_mday) + "-" + clock + "-et";
        }

        std::string ticker_from_hour_slug(const std::string &slug)
        {
            static const std::vector<std::pair<std::string, std::string>> names = {
                {"bitcoin-", "btc"}, {"ethereum-", "eth"}, {"xrp-", "xrp"},
                {"solana-", "sol"}};
            for (const auto &[prefix, ticker] : names)
            {
                if (slug.starts_with(prefix))
                {
                    return ticker;
                }
            }
            return {};
        }

        std::vector<uint64_t> aligned_market_timestamps(uint64_t now, uint64_t interval,
                                                        int count)
        {
            std::vector<uint64_t> timestamps;
            if (interval == 0 || count <= 0)
            {
                return timestamps;
            }
            const uint64_t current = now / interval * interval;
            for (int index = 0; index < count; ++index)
            {
                timestamps.push_back(current + interval * static_cast<uint64_t>(index));
            }
            return timestamps;
        }
    }
}
