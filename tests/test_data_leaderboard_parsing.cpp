#include "check_support.hpp"
#include "data_leaderboard_fixtures.hpp"
#include "data_parsing.hpp"

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

    nlohmann::json winner_row()
    {
        return nlohmann::json::parse(data_fixtures::data_biggest_winners_page).at("data").at(0);
    }

    nlohmann::json combo_row()
    {
        auto row = winner_row();
        row["kind"] = "combo";
        row["condition_id"] = "0x03ABCDEF00000000000000000000000000000000000000000000000000000001";
        row["position_id"] = "4242";
        row.erase("event_id");
        row.erase("event_slug");
        return row;
    }

    void expect_winner_rejected(const std::string &name, nlohmann::json row)
    {
        expect_rejected(name, [&] { (void)detail::parse_data_biggest_winner(row); });
    }

    void test_recorded_leaderboard()
    {
        const auto page =
            detail::parse_data_trader_leaderboard_page(data_fixtures::data_trader_leaderboard_page);
        check(page.items.size() == 2 && !page.next_cursor.empty(),
              "recorded leaderboard page must parse with a cursor");
        const auto &first = page.items.at(0);
        check(first.rank == 1 && first.wallet == "0x6666666666666666666666666666666666666666" &&
                  first.pnl > 3.2e6 && first.volume > 1.9e7 &&
                  first.user_name == "Example Trader 1" && first.x_username.empty() &&
                  !first.verified,
              "recorded leaderboard entry must parse");

        const auto standing =
            detail::parse_data_trader_standing(data_fixtures::data_trader_standing);
        check(standing && standing->pnl < -66000.0 && standing->pnl_rank == 507935 &&
                  standing->volume_rank == 36 && standing->x_username == "example_x",
              "recorded standing must parse");
        check(!detail::parse_data_trader_standing(R"({"data":null})"),
              "an unknown trader must have no standing");
        auto unranked = standing->raw;
        unranked["rank_pnl"] = 0;
        unranked.erase("rank_volume");
        const auto none =
            detail::parse_data_trader_standing(nlohmann::json{{"data", unranked}}.dump());
        check(none && !none->pnl_rank && !none->volume_rank, "rank 0 must read as unranked");
    }

    void test_biggest_winners()
    {
        const auto page =
            detail::parse_data_biggest_winners_page(data_fixtures::data_biggest_winners_page);
        check(page.items.size() == 3 && !page.next_cursor.empty(),
              "recorded biggest winners must parse with a cursor");
        const auto &market = page.items.at(0);
        check(market.kind == "market" && market.rank == 1 && market.pnl > 1.5e6 &&
                  market.resolved_at == 1790289740 && market.event_id == "1003664" &&
                  market.event_slug == "unl-nor-den-2026-09-24" &&
                  market.condition_id.starts_with("0x80c7ab79") &&
                  market.asset_id.starts_with("2330596927"),
              "a market winner must parse its market fields");

        auto row = combo_row();
        row["resolved_at"] = "2026-10-17T22:23:50Z";
        const auto combo = detail::parse_data_biggest_winner(row);
        check(combo.kind == "combo" &&
                  combo.condition_id ==
                      "0x03abcdef000000000000000000000000000000000000000000000000000000" &&
                  combo.asset_id == "4242" && combo.event_id.empty() && combo.event_slug.empty() &&
                  combo.resolved_at == 1792275830 && combo.event_title == market.event_title,
              "a combo winner must keep its common fields");

        auto text_id = winner_row();
        text_id["event_id"] = "77";
        check(detail::parse_data_biggest_winner(text_id).event_id == "77",
              "a text event ID must parse");
    }

    void test_malformed_input()
    {
        auto kind = winner_row();
        kind["kind"] = "parlay";
        expect_winner_rejected("an unknown winner kind", kind);
        auto zero = winner_row();
        zero["event_id"] = 0;
        expect_winner_rejected("a zero event ID", zero);
        auto missing = winner_row();
        missing.erase("event_id");
        expect_winner_rejected("a market winner without an event", missing);
        auto market_combo = combo_row();
        market_combo["condition_id"] = winner_row()["condition_id"];
        expect_winner_rejected("a combo with a market condition", market_combo);
        auto no_position = combo_row();
        no_position.erase("position_id");
        expect_winner_rejected("a combo without a position", no_position);
        expect_rejected("an entry without a rank",
                        []
                        {
                            (void)detail::parse_data_trader_entry(nlohmann::json::parse(
                                R"({"user_id":"0x1","pnl":1,"volume":1,"verified":false})"));
                        });
        expect_rejected("a standing list",
                        [] { (void)detail::parse_data_trader_standing(R"({"data":[]})"); });
    }
} // namespace

int main()
{
    test_recorded_leaderboard();
    test_biggest_winners();
    test_malformed_input();
    return check_support::finish("test_data_leaderboard_parsing");
}
