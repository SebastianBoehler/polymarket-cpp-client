#include "sports_parsing.hpp"

#include "stream_time.hpp"

#include <string>

namespace polymarket::detail
{
    namespace
    {
        bool all_digits(const std::string &text)
        {
            return !text.empty() && text.find_first_not_of("0123456789") == std::string::npos;
        }

        std::int64_t required_integer(const json &item, const char *key)
        {
            const auto *value = find_field(item, key);
            if (value != nullptr && value->is_string())
            {
                const auto &text = value->get_ref<const std::string &>();
                if (!all_digits(text) || text.size() > 18) throw_bad_field(key, "an integer");
                return std::stoll(text);
            }
            const auto number = field_integer(item, key);
            if (!number) throw_bad_field(key, "an integer");
            return *number;
        }

        bool required_bool(const json &item, const char *key)
        {
            const auto value = field_bool(item, key);
            if (!value) throw_bad_field(key, "a boolean");
            return *value;
        }
    } // namespace

    SportsGameResult parse_sports_result(const json &item)
    {
        SportsGameResult result;
        result.game_id = required_integer(item, "gameId");
        result.sportradar_game_id = field_string(item, "sportradarGameId");
        result.slug = field_string(item, "slug");
        result.league_abbreviation = field_required_string(item, "leagueAbbreviation");
        result.home_team = field_string(item, "homeTeam");
        result.away_team = field_string(item, "awayTeam");
        result.status = field_required_string(item, "status");
        result.live = required_bool(item, "live");
        result.ended = required_bool(item, "ended");
        result.score = field_required_string(item, "score");
        result.period = field_string(item, "period");
        result.elapsed = field_string(item, "elapsed");
        result.turn = field_string(item, "turn");
        result.finished_at_ms = stream_epoch_ms_or_iso(item, "finishedTimestamp");
        if (!result.finished_at_ms)
            result.finished_at_ms = stream_epoch_ms_or_iso(item, "finished_timestamp");
        result.raw = item;
        return result;
    }

    SportsGameResult parse_sports_message(const std::string &message)
    {
        return parse_sports_result(json::parse(message));
    }
} // namespace polymarket::detail
