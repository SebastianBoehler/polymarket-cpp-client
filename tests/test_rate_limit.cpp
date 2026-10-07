#include "check_support.hpp"
#include "polymarket/sdk_error.hpp"
#include "rate_limit_internal.hpp"

#include <stdexcept>
#include <string>

namespace
{
    using check_support::check;
    using polymarket::HttpResponse;
    using polymarket::RateLimitRetry;
    using polymarket::RateLimitUpdate;
    using namespace polymarket::detail;

    // RFC 9110's example date, Sun, 06 Nov 1994 08:49:37 GMT.
    constexpr std::int64_t example_date = 784111777;

    HttpResponse response_with(std::map<std::string, std::string> headers, std::string body = "",
                               long status = 429)
    {
        HttpResponse response;
        response.status_code = status;
        response.headers = std::move(headers);
        response.body = std::move(body);
        return response;
    }

    std::optional<double> retry_after(const std::string &value,
                                      std::int64_t now = example_date - 10)
    {
        return parse_retry_after_seconds(response_with({{"retry-after", value}}), now);
    }

    void test_retry_after_seconds()
    {
        check(retry_after("2") == 2.0, "integer Retry-After");
        check(retry_after("1.5") == 1.5, "decimal Retry-After");
        check(retry_after("0") == 0.0, "zero Retry-After");
        check(!retry_after("-1") && !retry_after("") && !retry_after("soon") && !retry_after("1."),
              "malformed Retry-After must be ignored");
    }

    void test_retry_after_http_dates()
    {
        check(retry_after("Sun, 06 Nov 1994 08:49:37 GMT") == 10.0, "IMF-fixdate Retry-After");
        check(retry_after("Sunday, 06-Nov-94 08:49:37 GMT") == 10.0, "RFC 850 Retry-After");
        check(retry_after("Sun Nov  6 08:49:37 1994") == 10.0, "asctime Retry-After");
        check(retry_after("Sun, 06 Nov 1994 08:49:37 GMT", example_date + 60) == 0.0,
              "a past HTTP date must clamp to zero");
        check(!retry_after("Sun, 06 Foo 1994 08:49:37 GMT") &&
                  !retry_after("Sun, 06 Nov 1994 08:49:37 UTC"),
              "malformed HTTP dates must be ignored");
    }

    void test_retry_after_body_field()
    {
        check(parse_retry_after_seconds(response_with({}, R"({"retry_after_seconds":3})"), 0) ==
                  3.0,
              "retry_after_seconds body field");
        check(parse_retry_after_seconds(
                  response_with({{"retry-after", "1"}}, R"({"retry_after_seconds":3})"), 0) == 1.0,
              "the Retry-After header must win over the body field");
        check(!parse_retry_after_seconds(response_with({}, R"({"retry_after_seconds":-3})"), 0) &&
                  !parse_retry_after_seconds(response_with({}, "not json"), 0) &&
                  !parse_retry_after_seconds(response_with({}), 0),
              "absent or invalid body fields must be ignored");
    }

    void test_rate_limit_update_headers()
    {
        const auto update =
            parse_rate_limit_update(response_with({{"poly-ratelimit-remaining", "-5"},
                                                   {"poly-ratelimit-reset", "1700000000"},
                                                   {"poly-ratelimit-tier", "standard"},
                                                   {"poly-ratelimit-warning", "TRUE"}}),
                                    RateLimitUpdate::Bucket::Order);
        check(update && update->bucket == RateLimitUpdate::Bucket::Order &&
                  update->remaining == -5 && update->reset == 1700000000 &&
                  update->tier == "standard" && update->warning,
              "all Poly-RateLimit headers must parse");

        const auto partial =
            parse_rate_limit_update(response_with({{"poly-ratelimit-remaining", "1.5"},
                                                   {"poly-ratelimit-reset", "-1"},
                                                   {"poly-ratelimit-tier", "gold"}}),
                                    RateLimitUpdate::Bucket::None);
        check(partial && !partial->remaining && !partial->reset && partial->tier == "gold" &&
                  !partial->warning,
              "malformed values must be dropped independently");

        check(!parse_rate_limit_update(response_with({{"poly-ratelimit-warning", "false"}}),
                                       RateLimitUpdate::Bucket::None) &&
                  !parse_rate_limit_update(response_with({}), RateLimitUpdate::Bucket::None),
              "responses without rate-limit state must yield nullopt");
    }

    void test_rate_limit_delay()
    {
        using std::chrono::milliseconds;
        check(rate_limit_delay(response_with({})) == milliseconds(1000),
              "missing Retry-After waits 1 s");
        check(rate_limit_delay(response_with({{"retry-after", "0.25"}})) == milliseconds(250),
              "Retry-After converts to milliseconds");
    }

    void test_retry_loop()
    {
        int attempts = 0;
        const auto limited_then_ok = [&attempts]
        {
            ++attempts;
            return attempts < 3 ? response_with({{"retry-after", "0"}})
                                : response_with({}, "{}", 200);
        };
        check(retry_rate_limited(RateLimitRetry{}, limited_then_ok).status_code == 200 &&
                  attempts == 3,
              "two 429s must be retried under the default policy");

        attempts = 0;
        check(
            retry_rate_limited(RateLimitRetry{1, std::chrono::milliseconds(5000)}, limited_then_ok)
                        .status_code == 429 &&
                attempts == 2,
            "retries must stop at the configured count");

        attempts = 0;
        check(retry_rate_limited(std::nullopt, limited_then_ok).status_code == 429 && attempts == 1,
              "a disabled policy must not retry");

        attempts = 0;
        const auto long_wait = [&attempts]
        {
            ++attempts;
            return response_with({{"retry-after", "10"}});
        };
        check(retry_rate_limited(RateLimitRetry{}, long_wait).status_code == 429 && attempts == 1,
              "a delay above max_delay must return the 429 without waiting");

        attempts = 0;
        const auto server_error = [&attempts]
        {
            ++attempts;
            return response_with({{"retry-after", "0"}}, "", 503);
        };
        check(retry_rate_limited(RateLimitRetry{}, server_error).status_code == 503 &&
                  attempts == 1,
              "only HTTP 429 is retried");
    }

    void test_policy_validation()
    {
        check_support::expect_throws<std::invalid_argument>(
            "negative retries",
            [] { validate_rate_limit_retry({-1, std::chrono::milliseconds(0)}); });
        check_support::expect_throws<std::invalid_argument>(
            "negative max_delay",
            [] { validate_rate_limit_retry({1, std::chrono::milliseconds(-1)}); });
        validate_rate_limit_retry({0, std::chrono::milliseconds(0)});
    }

    void test_sdk_error_fields()
    {
        const auto error = polymarket::make_sdk_error(
            response_with({{"retry-after", "2"}, {"poly-ratelimit-remaining", "0"}},
                          R"({"error":"slow down"})"),
            "/book");
        check(error.code == polymarket::SdkErrorCode::RateLimit &&
                  error.retry_after_seconds == 2.0 && error.rate_limit &&
                  error.rate_limit->remaining == 0,
              "make_sdk_error must carry Retry-After and rate-limit state");

        HttpResponse transport_failure;
        transport_failure.error = "connection refused";
        const auto transport = polymarket::make_sdk_error(transport_failure, "/book");
        check(!transport.retry_after_seconds && !transport.rate_limit,
              "transport failures carry no rate-limit fields");
    }
} // namespace

int main()
{
    test_retry_after_seconds();
    test_retry_after_http_dates();
    test_retry_after_body_field();
    test_rate_limit_update_headers();
    test_rate_limit_delay();
    test_retry_loop();
    test_policy_validation();
    test_sdk_error_fields();
    return check_support::finish("test_rate_limit");
}
