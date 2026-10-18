#pragma once

#include "polymarket/gamma_social_types.hpp"
#include "polymarket/types.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace polymarket
{
    namespace detail
    {
        class RtdsStreamRuntime;
    }

    // Comment events to receive. `types` holds comment_created (the default
    // when empty), comment_removed, reaction_created or reaction_removed.
    // A parent filter keeps comments on that event or market and drops
    // reactions, which carry no parent.
    struct RtdsCommentFilter
    {
        std::vector<std::string> types;
        std::optional<std::int64_t> parent_entity_id;
        std::string parent_entity_type; // Event or Market
    };

    // One comment event, following the official py-sdk models. `comment` is
    // set for comment_created and comment_removed, `reaction` for the
    // reaction events. `raw` holds the whole frame as received.
    struct RtdsCommentEvent
    {
        std::string type;
        std::optional<std::int64_t> timestamp_ms; // Unix milliseconds
        std::optional<GammaComment> comment;
        std::optional<GammaReaction> reaction;
        nlohmann::json raw;
    };

    // Live crypto prices from one source: binance (USDT pairs such as btcusdt)
    // or chainlink (pairs such as btc/usd). Every symbol when empty.
    struct RtdsCryptoPriceFilter
    {
        std::string source;
        std::vector<std::string> symbols;
    };

    // Chainlink TWAPs over a 30- or 60-second window. Every symbol when empty.
    struct RtdsCryptoTwapFilter
    {
        int window_seconds{60};
        std::vector<std::string> symbols;
    };

    // Pyth prices for one symbol, matched case-insensitively. `types` holds
    // update and/or subscribe (history snapshots); both when empty.
    struct RtdsEquityPriceFilter
    {
        std::string symbol;
        std::vector<std::string> types;
    };

    // `exact_value` is the most precise decimal the frame carries
    // (full_accuracy_value, with Chainlink's 18-decimal integers scaled);
    // `value` is the same number as a double.
    struct RtdsPricePoint
    {
        std::int64_t timestamp_ms{0}; // Unix milliseconds
        double value{0.0};
        std::string exact_value;
    };

    // One price event, following the official py-sdk models. `topic` is
    // prices.crypto.binance, prices.crypto.chainlink,
    // prices.crypto.chainlink.twap or prices.equity.pyth. An update carries
    // `price`; an equity subscribe event carries `history` instead. Equity
    // prices use the instrument's quote currency; crypto prices are USD, or
    // USDT for Binance. `raw` holds the whole frame as received.
    struct RtdsPriceEvent
    {
        std::string topic;
        std::string type; // update or subscribe
        std::optional<std::int64_t> timestamp_ms;
        std::string symbol;
        RtdsPricePoint price;
        std::optional<int> window_seconds;       // TWAP
        std::optional<std::int64_t> received_at; // equity, as sent
        std::optional<bool> is_carried_forward;  // equity
        std::vector<RtdsPricePoint> history;     // equity subscribe
        nlohmann::json raw;
    };

    using RtdsSubscriptionId = std::uint64_t;
    using RtdsCommentCallback = std::function<void(const RtdsCommentEvent &event)>;
    using RtdsPriceCallback = std::function<void(const RtdsPriceEvent &event)>;
    // Fired whenever events may have been missed: every connect, disconnect,
    // queue overflow or idle reconnect. RTDS does not replay them.
    using RtdsStreamGapCallback = std::function<void()>;
    // Fired once the connection and its subscriptions are back after a gap.
    using RtdsStreamRecoveredCallback = std::function<void()>;
    using RtdsStreamErrorCallback = std::function<void(const std::string &error)>;

    // Public real-time data stream (Config::rtds_ws_url). Subscriptions can
    // be added and removed before or after connect(); the stream sends the
    // combined server subscription and restores it after every reconnect.
    // It pings every 5 seconds and reconnects when no frame arrives for 10
    // minutes. Callbacks run on the stream's own threads; a malformed or
    // unknown frame is counted in dropped_events() and skipped.
    class RtdsStream
    {
      public:
        explicit RtdsStream(const Config &config = Config{});
        ~RtdsStream();

        RtdsStream(const RtdsStream &) = delete;
        RtdsStream &operator=(const RtdsStream &) = delete;

        // Throws std::invalid_argument for an unknown type or parent type.
        RtdsSubscriptionId subscribe_comments(const RtdsCommentFilter &filter,
                                              RtdsCommentCallback callback);
        // Price topics are subscribed whole and narrowed here, as the official
        // SDKs do, because RTDS keeps one filter per topic and connection.
        // The official SDKs deprecate these topics in favour of the
        // authenticated price service. Throw std::invalid_argument for an
        // unknown source, window or type, or an empty symbol.
        RtdsSubscriptionId subscribe_crypto_prices(const RtdsCryptoPriceFilter &filter,
                                                   RtdsPriceCallback callback);
        RtdsSubscriptionId subscribe_crypto_twap(const RtdsCryptoTwapFilter &filter,
                                                 RtdsPriceCallback callback);
        RtdsSubscriptionId subscribe_equity_prices(const RtdsEquityPriceFilter &filter,
                                                   RtdsPriceCallback callback);
        // Unknown IDs are ignored.
        void unsubscribe(RtdsSubscriptionId id);

        void on_stream_gap(RtdsStreamGapCallback callback);
        void on_stream_recovered(RtdsStreamRecoveredCallback callback);
        void on_error(RtdsStreamErrorCallback callback);

        // Waits up to Config::ws_connect_timeout_ms for the connection.
        bool connect();
        void disconnect();
        bool is_connected() const;
        // Blocks until stop().
        void run();
        void stop();

        std::uint64_t events() const;
        std::uint64_t dropped_events() const;

      private:
        std::shared_ptr<detail::RtdsStreamRuntime> runtime_;
    };

} // namespace polymarket
