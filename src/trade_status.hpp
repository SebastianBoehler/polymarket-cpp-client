#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace polymarket::detail
{
    inline std::string normalized_trade_status(const std::string &status)
    {
        constexpr std::string_view prefix = "TRADE_STATUS_";
        std::string normalized = status;
        std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                       [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        if (normalized.starts_with(prefix)) normalized.erase(0, prefix.size());
        return normalized;
    }

    // CONFIRMED is final on-chain; FAILED never will be. Earlier statuses
    // (MATCHED, MINED, RETRYING) can still change their transaction hash.
    inline bool is_settled_trade_status(const std::string &status)
    {
        const auto normalized = normalized_trade_status(status);
        return normalized == "CONFIRMED" || normalized == "FAILED";
    }

    inline bool is_failed_trade_status(const std::string &status)
    {
        return normalized_trade_status(status) == "FAILED";
    }
} // namespace polymarket::detail
