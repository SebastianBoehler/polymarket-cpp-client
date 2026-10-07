#include "rate_limit_internal.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string_view>

namespace polymarket::detail
{
    namespace
    {
        const std::string *header(const HttpResponse &response, const char *lowercase_key)
        {
            const auto it = response.headers.find(lowercase_key);
            return it == response.headers.end() ? nullptr : &it->second;
        }

        bool all_digits(std::string_view text)
        {
            return !text.empty() && std::all_of(text.begin(), text.end(), [](unsigned char c)
                                                { return std::isdigit(c) != 0; });
        }

        // Non-negative decimal such as "2" or "1.5".
        std::optional<double> parse_delay_seconds(std::string_view text)
        {
            const auto dot = text.find('.');
            const auto whole = text.substr(0, dot);
            const auto fraction =
                dot == std::string_view::npos ? std::string_view{} : text.substr(dot + 1);
            if (!all_digits(whole) || (dot != std::string_view::npos && !all_digits(fraction)) ||
                whole.size() > 9)
                return std::nullopt;
            double value = 0.0;
            for (const char c : whole)
                value = value * 10 + (c - '0');
            double scale = 0.1;
            for (const char c : fraction)
            {
                value += (c - '0') * scale;
                scale /= 10;
            }
            return value;
        }

        std::optional<long long> parse_integer(std::string_view text, bool allow_negative)
        {
            const bool negative = allow_negative && !text.empty() && text.front() == '-';
            const auto digits = negative ? text.substr(1) : text;
            if (!all_digits(digits) || digits.size() > 18) return std::nullopt;
            long long value = 0;
            for (const char c : digits)
                value = value * 10 + (c - '0');
            return negative ? -value : value;
        }

        // Days since 1970-01-01 for a proleptic Gregorian date.
        std::int64_t days_from_civil(std::int64_t year, unsigned month, unsigned day)
        {
            year -= month <= 2;
            const std::int64_t era = (year >= 0 ? year : year - 399) / 400;
            const auto year_of_era = static_cast<unsigned>(year - era * 400);
            const unsigned day_of_year = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
            const unsigned day_of_era =
                year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
            return era * 146097 + static_cast<std::int64_t>(day_of_era) - 719468;
        }

        int month_number(const char *name)
        {
            static constexpr std::array<const char *, 12> months = {
                "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
            for (std::size_t i = 0; i < months.size(); ++i)
                if (std::string_view(name) == months[i]) return static_cast<int>(i) + 1;
            return 0;
        }

        // RFC 9110 section 5.6.7: IMF-fixdate plus the obsolete RFC 850 and
        // asctime forms, all read as UTC.
        std::optional<std::int64_t> parse_http_date(const std::string &text)
        {
            char weekday[16] = {};
            char month[4] = {};
            int day = 0;
            int year = 0;
            int hour = 0;
            int minute = 0;
            int second = 0;
            int consumed = 0;
            bool parsed =
                std::sscanf(text.c_str(), "%3s, %2d %3s %4d %2d:%2d:%2d GMT%n", weekday, &day,
                            month, &year, &hour, &minute, &second, &consumed) == 7 &&
                static_cast<std::size_t>(consumed) == text.size();
            if (!parsed)
            {
                parsed =
                    std::sscanf(text.c_str(), "%15[A-Za-z], %2d-%3s-%2d %2d:%2d:%2d GMT%n", weekday,
                                &day, month, &year, &hour, &minute, &second, &consumed) == 7 &&
                    static_cast<std::size_t>(consumed) == text.size();
                if (parsed) year += year < 70 ? 2000 : 1900;
            }
            if (!parsed)
                parsed = std::sscanf(text.c_str(), "%3s %3s %2d %2d:%2d:%2d %4d%n", weekday, month,
                                     &day, &hour, &minute, &second, &year, &consumed) == 7 &&
                         static_cast<std::size_t>(consumed) == text.size();
            const int month_index = month_number(month);
            if (!parsed || month_index == 0 || day < 1 || day > 31 || hour > 23 || minute > 59 ||
                second > 60)
                return std::nullopt;
            return days_from_civil(year, static_cast<unsigned>(month_index),
                                   static_cast<unsigned>(day)) *
                       86400 +
                   hour * 3600 + minute * 60 + second;
        }

        std::optional<double> retry_after_from_body(const std::string &body)
        {
            if (body.empty()) return std::nullopt;
            try
            {
                const auto parsed = nlohmann::json::parse(body);
                if (parsed.is_object() && parsed.contains("retry_after_seconds"))
                {
                    const auto &value = parsed["retry_after_seconds"];
                    if (value.is_number() && value.get<double>() >= 0) return value.get<double>();
                }
            }
            catch (const nlohmann::json::exception &)
            {
            }
            return std::nullopt;
        }
    } // namespace

    std::optional<double> parse_retry_after_seconds(const HttpResponse &response,
                                                    std::int64_t now_unix_seconds)
    {
        if (const auto *value = header(response, "retry-after"))
        {
            if (const auto seconds = parse_delay_seconds(*value)) return seconds;
            if (const auto date = parse_http_date(*value))
                return static_cast<double>(std::max<std::int64_t>(0, *date - now_unix_seconds));
        }
        return retry_after_from_body(response.body);
    }

    std::optional<double> parse_retry_after_seconds(const HttpResponse &response)
    {
        const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
        return parse_retry_after_seconds(response, now);
    }

    std::optional<RateLimitUpdate> parse_rate_limit_update(const HttpResponse &response,
                                                           RateLimitUpdate::Bucket bucket)
    {
        RateLimitUpdate update;
        update.bucket = bucket;
        if (const auto *value = header(response, "poly-ratelimit-remaining"))
            update.remaining = parse_integer(*value, true);
        if (const auto *value = header(response, "poly-ratelimit-reset"))
            update.reset = parse_integer(*value, false);
        if (const auto *value = header(response, "poly-ratelimit-tier"); value && !value->empty())
            update.tier = *value;
        if (const auto *value = header(response, "poly-ratelimit-warning"))
        {
            std::string lowered = *value;
            std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            update.warning = lowered == "true";
        }
        if (!update.remaining && !update.reset && !update.tier && !update.warning)
            return std::nullopt;
        return update;
    }

    std::chrono::milliseconds rate_limit_delay(const HttpResponse &response)
    {
        const auto seconds = parse_retry_after_seconds(response).value_or(1.0);
        return std::chrono::milliseconds(static_cast<long long>(std::ceil(seconds * 1000)));
    }

    void validate_rate_limit_retry(const RateLimitRetry &policy)
    {
        if (policy.retries < 0)
            throw std::invalid_argument("RateLimitRetry.retries must be non-negative");
        if (policy.max_delay < std::chrono::milliseconds::zero())
            throw std::invalid_argument("RateLimitRetry.max_delay must be non-negative");
    }

} // namespace polymarket::detail
