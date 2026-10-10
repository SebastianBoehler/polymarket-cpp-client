#pragma once

#include "polymarket/http_client.hpp"
#include "polymarket/rate_limit.hpp"
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <thread>

namespace polymarket::detail
{
    // Retry-After as seconds or an HTTP date (relative to now_unix_seconds),
    // else a retry_after_seconds body field. Never negative; nullopt when absent
    // or malformed.
    std::optional<double> parse_retry_after_seconds(const HttpResponse &response,
                                                    std::int64_t now_unix_seconds);
    std::optional<double> parse_retry_after_seconds(const HttpResponse &response);

    // Nullopt when the response carries none of the Poly-RateLimit-* headers.
    std::optional<RateLimitUpdate> parse_rate_limit_update(const HttpResponse &response,
                                                           RateLimitUpdate::Bucket bucket);

    // Delay before retrying a 429: the server's Retry-After, or 1 s.
    std::chrono::milliseconds rate_limit_delay(const HttpResponse &response);

    void validate_rate_limit_retry(const RateLimitRetry &policy);

    // Runs attempt() and retries it while the response is HTTP 429, sleeping
    // the server-requested delay between attempts. attempt() must rebuild any
    // signed headers so each retry carries a fresh timestamp.
    template <typename Attempt>
    HttpResponse
    retry_rate_limited(const std::optional<RateLimitRetry> &policy, Attempt &&attempt,
                       std::optional<std::chrono::steady_clock::time_point> deadline = std::nullopt)
    {
        for (int retry = 0;; ++retry)
        {
            auto response = attempt();
            if (response.status_code != 429 || !policy || retry >= policy->retries) return response;
            const auto delay = rate_limit_delay(response);
            if (delay > policy->max_delay) return response;
            if (deadline && std::chrono::steady_clock::now() + delay > *deadline) return response;
            std::this_thread::sleep_for(delay);
        }
    }

} // namespace polymarket::detail
