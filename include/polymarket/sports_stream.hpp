#pragma once

#include "polymarket/types.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace polymarket
{
    namespace detail
    {
        class SportsStreamRuntime;
    }

    // One live game update from the Sports WebSocket, following the official
    // py-sdk model. Absent or empty text stays empty; `raw` holds the whole
    // object as received.
    struct SportsGameResult
    {
        std::int64_t game_id{0};
        std::string sportradar_game_id;
        std::string slug;
        std::string league_abbreviation;
        std::string home_team;
        std::string away_team;
        std::string status;
        bool live{false};
        bool ended{false};
        std::string score;
        std::string period;
        std::string elapsed;
        std::string turn;
        std::optional<std::int64_t> finished_at_ms; // Unix milliseconds
        nlohmann::json raw;
    };

    using SportsResultCallback = std::function<void(const SportsGameResult &result)>;
    // Fired whenever updates may have been missed: every connect, disconnect,
    // queue overflow or idle reconnect. The server does not replay them.
    using SportsStreamGapCallback = std::function<void()>;
    // Fired once the connection is back after a gap; refresh game state from
    // GammaClient here if it matters.
    using SportsStreamRecoveredCallback = std::function<void()>;
    using SportsStreamErrorCallback = std::function<void(const std::string &error)>;

    // Public Sports stream (Config::sports_ws_url). Every connection receives
    // every game update; there are no filters. The stream answers the
    // server's heartbeat and reconnects when no frame arrives for 30 seconds.
    // Callbacks run on the stream's own threads; a malformed update is
    // counted in dropped_events() and skipped.
    class SportsStream
    {
      public:
        explicit SportsStream(const Config &config = Config{});
        ~SportsStream();

        SportsStream(const SportsStream &) = delete;
        SportsStream &operator=(const SportsStream &) = delete;

        void on_result(SportsResultCallback callback);
        void on_stream_gap(SportsStreamGapCallback callback);
        void on_stream_recovered(SportsStreamRecoveredCallback callback);
        void on_error(SportsStreamErrorCallback callback);

        // Waits up to Config::ws_connect_timeout_ms for the connection.
        bool connect();
        void disconnect();
        bool is_connected() const;
        // Blocks until stop().
        void run();
        void stop();

        std::uint64_t result_events() const;
        std::uint64_t dropped_events() const;

      private:
        std::shared_ptr<detail::SportsStreamRuntime> runtime_;
    };

} // namespace polymarket
