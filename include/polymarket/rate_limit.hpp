#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace polymarket
{
    // Per-signer rate-limit state from the Poly-RateLimit-* response headers.
    // Each field is set only when its header is present and well formed.
    struct RateLimitUpdate
    {
        enum class Bucket : std::uint8_t
        {
            None,
            Order,
            Cancel
        };

        Bucket bucket{Bucket::None};
        // Tokens left in the bucket; may be negative. Zero alone does not mean
        // the bucket is exhausted, so do not back off on it.
        std::optional<long long> remaining;
        std::optional<long long> reset; // Unix seconds when the wait period ends
        std::optional<std::string> tier;
        // True when the limiter runs in warning mode and would have rejected
        // this request under enforcement.
        bool warning{false};
    };

    // Retries for HTTP 429 on read requests. Each retry waits exactly the
    // server's Retry-After delay (1 s when absent); a longer requested delay
    // than max_delay returns the error instead.
    struct RateLimitRetry
    {
        int retries{2}; // after the first attempt
        std::chrono::milliseconds max_delay{5000};
    };

} // namespace polymarket
