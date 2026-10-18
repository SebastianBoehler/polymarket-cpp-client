#include "rest_numeric.hpp"
#include "rtds_parsing.hpp"
#include "stream_time.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>
#include <utility>

// Price topics, following py-sdk models/rtds_events.py and
// _internal/streams/rtds/protocol.py.
namespace polymarket::detail
{
    namespace
    {
        constexpr const char *binance_topic = "prices.crypto.binance";
        constexpr const char *chainlink_topic = "prices.crypto.chainlink";
        constexpr const char *twap_topic = "prices.crypto.chainlink.twap";
        constexpr const char *equity_topic = "prices.equity.pyth";

        bool signed_integer(const std::string &text)
        {
            const auto digits = text.starts_with('-') ? text.substr(1) : text;
            return !digits.empty() && digits.find_first_not_of("0123456789") == std::string::npos;
        }

        std::string lowercase(std::string text)
        {
            std::transform(text.begin(), text.end(), text.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            return text;
        }

        std::string required_decimal(const json &item, const char *key)
        {
            auto text = field_decimal_text(item, key);
            if (text.empty()) throw_bad_field(key, "a decimal number");
            return text;
        }

        std::int64_t required_integer(const json &item, const char *key)
        {
            const auto value = field_integer(item, key);
            if (!value) throw_bad_field(key, "an integer");
            return *value;
        }

        RtdsPricePoint point(std::int64_t timestamp_ms, std::string exact)
        {
            const auto value = decimal_string_to_double(exact);
            return {timestamp_ms, value, std::move(exact)};
        }

        // The full-accuracy text when it is valid for the source, else `value`.
        std::string crypto_exact(const json &payload, bool chainlink)
        {
            auto value = required_decimal(payload, "value");
            const auto *full = find_field(payload, "full_accuracy_value");
            if (full == nullptr || !full->is_string()) return value;
            const auto &text = full->get_ref<const std::string &>();
            if (chainlink) return signed_integer(text) ? scaled_e18_decimal(text) : value;
            return is_decimal_number(text) ? text : value;
        }

        const json &payload_object(const json &frame)
        {
            const auto *payload = find_field(frame, "payload");
            if (payload == nullptr || !payload->is_object())
                throw_bad_field("payload", "an object");
            return *payload;
        }

        std::vector<std::string> distinct_symbols(const std::vector<std::string> &symbols)
        {
            std::vector<std::string> unique;
            for (const auto &symbol : symbols)
            {
                if (symbol.empty()) throw std::invalid_argument("symbol must be non-empty");
                if (std::find(unique.begin(), unique.end(), symbol) == unique.end())
                    unique.push_back(symbol);
            }
            return unique;
        }
    } // namespace

    std::string scaled_e18_decimal(const std::string &integer)
    {
        if (!signed_integer(integer))
            throw std::invalid_argument("full_accuracy_value must be a signed integer string");
        const bool negative = integer.starts_with('-');
        auto digits = negative ? integer.substr(1) : integer;
        digits.erase(0, std::min(digits.find_first_not_of('0'), digits.size() - 1));
        if (digits.size() <= 18) digits.insert(0, 19 - digits.size(), '0');
        auto whole = digits.substr(0, digits.size() - 18);
        auto fraction = digits.substr(digits.size() - 18);
        fraction.erase(fraction.find_last_not_of('0') + 1);
        auto text = whole + (fraction.empty() ? "" : "." + fraction);
        return negative && text != "0" ? "-" + text : text;
    }

    RtdsPriceEvent parse_rtds_price_event(const json &frame, const std::string &wire_topic)
    {
        RtdsPriceEvent event;
        event.type = field_required_string(frame, "type");
        event.timestamp_ms = stream_epoch_ms_or_iso(frame, "timestamp");
        const auto &payload = payload_object(frame);
        event.symbol = field_required_string(payload, "symbol");
        if (wire_topic == "equity_prices")
        {
            event.topic = equity_topic;
            if (event.type == "subscribe")
            {
                for (const auto &entry : field_objects(payload, "data"))
                    event.history.push_back(point(required_integer(entry, "timestamp"),
                                                  required_decimal(entry, "value")));
                if (find_field(payload, "data") == nullptr) throw_bad_field("data", "an array");
            }
            else if (event.type == "update")
            {
                const auto *full = find_field(payload, "full_accuracy_value");
                event.price = point(
                    required_integer(payload, "timestamp"),
                    required_decimal(payload, full != nullptr ? "full_accuracy_value" : "value"));
                event.received_at = field_integer(payload, "received_at");
                event.is_carried_forward = field_bool(payload, "is_carried_forward");
            }
            else
            {
                throw_bad_field("type", "update or subscribe");
            }
            event.raw = frame;
            return event;
        }

        if (event.type != "update") throw_bad_field("type", "update");
        const auto timestamp = required_integer(payload, "timestamp");
        if (wire_topic == "crypto_prices_twap_thirty" || wire_topic == "crypto_prices_twap_sixty")
        {
            event.topic = twap_topic;
            const int expected = wire_topic == "crypto_prices_twap_thirty" ? 30 : 60;
            const auto window = field_integer(payload, "window_s");
            if (!window || *window != expected)
                throw std::invalid_argument("RTDS TWAP topic " + wire_topic +
                                            " requires window_s=" + std::to_string(expected));
            event.window_seconds = expected;
            (void)required_decimal(payload, "value");
            const auto *full = find_field(payload, "full_accuracy_value");
            if (full == nullptr || !full->is_string())
                throw_bad_field("full_accuracy_value", "a signed integer string");
            event.price = point(timestamp, scaled_e18_decimal(full->get<std::string>()));
        }
        else if (wire_topic == "crypto_prices" || wire_topic == "crypto_prices_chainlink")
        {
            const bool chainlink = wire_topic == "crypto_prices_chainlink";
            event.topic = chainlink ? chainlink_topic : binance_topic;
            event.price = point(timestamp, crypto_exact(payload, chainlink));
        }
        else
        {
            throw std::invalid_argument("unknown RTDS topic " + wire_topic);
        }
        event.raw = frame;
        return event;
    }

    RtdsPriceMatch rtds_crypto_price_match(const RtdsCryptoPriceFilter &filter)
    {
        if (filter.source != "binance" && filter.source != "chainlink")
            throw std::invalid_argument("source must be binance or chainlink");
        auto symbols = distinct_symbols(filter.symbols);
        RtdsPriceMatch match;
        match.topic = filter.source == "binance" ? binance_topic : chainlink_topic;
        match.symbols = std::move(symbols);
        return match;
    }

    RtdsPriceMatch rtds_crypto_twap_match(const RtdsCryptoTwapFilter &filter)
    {
        if (filter.window_seconds != 30 && filter.window_seconds != 60)
            throw std::invalid_argument("window_seconds must be 30 or 60");
        auto symbols = distinct_symbols(filter.symbols);
        RtdsPriceMatch match;
        match.topic = twap_topic;
        match.symbols = std::move(symbols);
        match.window_seconds = filter.window_seconds;
        return match;
    }

    RtdsPriceMatch rtds_equity_price_match(const RtdsEquityPriceFilter &filter)
    {
        if (filter.symbol.empty()) throw std::invalid_argument("symbol must be a non-empty string");
        RtdsPriceMatch match{equity_topic, {lowercase(filter.symbol)}, std::nullopt, {}};
        for (const auto &type : filter.types)
        {
            if (type != "update" && type != "subscribe")
                throw std::invalid_argument("invalid equity event type: " + type);
            if (std::find(match.types.begin(), match.types.end(), type) == match.types.end())
                match.types.push_back(type);
        }
        return match;
    }

    std::vector<RtdsServerKey> rtds_price_keys(const RtdsPriceMatch &match)
    {
        if (match.topic == binance_topic) return {{"crypto_prices", "update", ""}};
        if (match.topic == chainlink_topic) return {{"crypto_prices_chainlink", "update", ""}};
        if (match.topic == twap_topic)
            return {{match.window_seconds == 30 ? "crypto_prices_twap_thirty"
                                                : "crypto_prices_twap_sixty",
                     "update", ""}};
        return {{"equity_prices", "update", ""}};
    }

    bool rtds_price_matches(const RtdsPriceMatch &match, const RtdsPriceEvent &event)
    {
        if (event.topic != match.topic) return false;
        if (match.topic == equity_topic)
        {
            return lowercase(event.symbol) == match.symbols.front() &&
                   (match.types.empty() || std::find(match.types.begin(), match.types.end(),
                                                     event.type) != match.types.end());
        }
        if (match.window_seconds && event.window_seconds != match.window_seconds) return false;
        return match.symbols.empty() || std::find(match.symbols.begin(), match.symbols.end(),
                                                  event.symbol) != match.symbols.end();
    }
} // namespace polymarket::detail
