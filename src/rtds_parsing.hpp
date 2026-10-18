#pragma once

#include "json_fields.hpp"
#include "polymarket/rtds_stream.hpp"

#include <optional>
#include <string>
#include <vector>

// RTDS frames, following py-sdk _internal/streams/rtds/protocol.py and
// models/rtds_events.py. Malformed input throws std::exception.
namespace polymarket::detail
{
    // One server subscription; `filters` is sent only when non-empty.
    struct RtdsServerKey
    {
        std::string topic; // wire topic
        std::string type;
        std::string filters;

        bool operator==(const RtdsServerKey &) const = default;
    };

    struct RtdsParsedEvent
    {
        std::string topic; // wire topic
        std::string type;
        std::optional<RtdsCommentEvent> comment;
        std::optional<RtdsPriceEvent> price;
    };

    // A price subscription with its source checked and symbols deduplicated.
    struct RtdsPriceMatch
    {
        std::string topic; // API topic
        std::vector<std::string> symbols;
        std::optional<int> window_seconds;
        std::vector<std::string> types;
    };

    // Unset for the empty frame RTDS sends after connecting.
    std::optional<RtdsParsedEvent> parse_rtds_message(const std::string &message);
    RtdsCommentEvent parse_rtds_comment_event(const json &frame);

    // {"action": action, "subscriptions": [...]} for `keys`.
    std::string rtds_subscription_frame(const char *action, const std::vector<RtdsServerKey> &keys);

    // Checked and deduplicated, with the default type when empty; throws
    // std::invalid_argument.
    RtdsCommentFilter normalized_comment_filter(const RtdsCommentFilter &filter);
    std::vector<RtdsServerKey> rtds_comment_keys(const RtdsCommentFilter &normalized);
    bool rtds_comment_matches(const RtdsCommentFilter &normalized, const RtdsCommentEvent &event);

    // "-12345" with 18 implied decimals becomes "-0.000000000000012345".
    std::string scaled_e18_decimal(const std::string &integer);
    RtdsPriceEvent parse_rtds_price_event(const json &frame, const std::string &wire_topic);

    // Checked filters as matches; throw std::invalid_argument.
    RtdsPriceMatch rtds_crypto_price_match(const RtdsCryptoPriceFilter &filter);
    RtdsPriceMatch rtds_crypto_twap_match(const RtdsCryptoTwapFilter &filter);
    RtdsPriceMatch rtds_equity_price_match(const RtdsEquityPriceFilter &filter);
    std::vector<RtdsServerKey> rtds_price_keys(const RtdsPriceMatch &match);
    bool rtds_price_matches(const RtdsPriceMatch &match, const RtdsPriceEvent &event);
} // namespace polymarket::detail
