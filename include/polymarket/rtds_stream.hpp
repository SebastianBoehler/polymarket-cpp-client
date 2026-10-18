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

    using RtdsSubscriptionId = std::uint64_t;
    using RtdsCommentCallback = std::function<void(const RtdsCommentEvent &event)>;
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
