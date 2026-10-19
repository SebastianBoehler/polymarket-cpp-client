#include "check_support.hpp"
#include "sports_fixtures.hpp"
#include "sports_parsing.hpp"

#include <functional>
#include <string>

namespace
{
    using check_support::check;
    namespace detail = polymarket::detail;

    void expect_rejected(const std::string &name, const std::function<void()> &parse)
    {
        try
        {
            parse();
        }
        catch (const std::exception &)
        {
            return;
        }
        check(false, name + " must be rejected");
    }

    nlohmann::json live_row()
    {
        return nlohmann::json::parse(sports_fixtures::sports_live_result);
    }

    std::optional<std::int64_t> finished(const nlohmann::json &value)
    {
        auto row = live_row();
        row["finishedTimestamp"] = value;
        return detail::parse_sports_result(row).finished_at_ms;
    }

    void test_recorded_results()
    {
        const auto live = detail::parse_sports_message(sports_fixtures::sports_live_result);
        check(live.game_id == 70918912 && live.league_abbreviation == "cfb" &&
                  live.home_team == "BYU" && live.away_team == "ND" &&
                  live.status == "inprogress" && live.score == "10-21" && live.period == "Q2" &&
                  live.elapsed == "00:33" && live.turn == "nd" && live.live && !live.ended &&
                  !live.finished_at_ms &&
                  live.sportradar_game_id == "568982f7-6051-4a30-8e7c-d9d6235f9472" &&
                  live.raw.contains("turnProviderId"),
              "a recorded live game must parse");

        const auto camel =
            detail::parse_sports_message(sports_fixtures::sports_camel_status_result);
        check(camel.status == "InProgress" && camel.league_abbreviation == "mls" &&
                  camel.score == "0-3" && camel.sportradar_game_id.empty() && camel.turn.empty(),
              "a status is kept as sent");
    }

    void test_finished_timestamps()
    {
        check(finished(1792290000123) == 1792290000123, "epoch milliseconds must be kept");
        check(finished("1792290000123") == 1792290000123, "a digit string is milliseconds");
        check(finished("2026-10-18T03:40:00Z") == 1792294800000, "a UTC time must parse");
        check(finished("2026-10-18T03:40:00.5Z") == 1792294800500, "fractions must parse");
        check(finished("2026-10-18T05:40:00+02:00") == 1792294800000, "an offset must apply");
        check(finished("2026-10-17T22:40:00-05:00") == 1792294800000,
              "a negative offset must apply");
        check(finished("2026-10-18T03:40:00") == 1792294800000, "a zoneless time is UTC");
        check(!finished("") && !finished(nullptr), "an empty time is unset");

        auto snake = live_row();
        snake["finishedTimestamp"] = nullptr;
        snake["finished_timestamp"] = 1792290000000;
        check(detail::parse_sports_result(snake).finished_at_ms == 1792290000000,
              "finished_timestamp must fill a missing finishedTimestamp");

        auto text_id = live_row();
        text_id["gameId"] = "77";
        check(detail::parse_sports_result(text_id).game_id == 77, "a digit-string game ID parses");
    }

    void test_malformed_input()
    {
        expect_rejected(
            "a partial cricket update",
            [] { (void)detail::parse_sports_message(sports_fixtures::sports_partial_result); });
        expect_rejected(
            "an update without a score",
            [] { (void)detail::parse_sports_message(sports_fixtures::sports_unscored_result); });
        expect_rejected("non-JSON text", [] { (void)detail::parse_sports_message("ping"); });
        expect_rejected("a list", [] { (void)detail::parse_sports_message("[]"); });
        const auto rejected_field = [](const char *name, const char *key, nlohmann::json value)
        {
            auto row = live_row();
            row[key] = std::move(value);
            expect_rejected(name, [&] { (void)detail::parse_sports_result(row); });
        };
        rejected_field("a string live flag", "live", "true");
        rejected_field("a missing score", "score", nullptr);
        rejected_field("a fractional game ID", "gameId", 1.5);
        rejected_field("a malformed finish time", "finishedTimestamp", "18 Oct 2026");
        rejected_field("a boolean finish time", "finishedTimestamp", true);
        rejected_field("an oversized finish time", "finishedTimestamp", "9999999999999999");
    }
} // namespace

int main()
{
    test_recorded_results();
    test_finished_timestamps();
    test_malformed_input();
    return check_support::finish("test_sports_parsing");
}
