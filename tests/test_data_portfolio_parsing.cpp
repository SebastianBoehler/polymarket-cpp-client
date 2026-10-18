#include "check_support.hpp"
#include "data_parsing.hpp"
#include "data_portfolio_fixtures.hpp"

#include <cmath>
#include <functional>
#include <string>

namespace
{
    using check_support::check;
    using check_support::expect_equal;
    namespace detail = polymarket::detail;

    bool near(double value, double expected)
    {
        return std::abs(value - expected) < 1e-9 * std::max(1.0, std::abs(expected));
    }

    bool near(const std::optional<double> &value, double expected)
    {
        return value && near(*value, expected);
    }

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

    nlohmann::json recorded_position()
    {
        return nlohmann::json::parse(data_fixtures::data_position_page).at("data").at(1);
    }

    void expect_position_rejected(const std::string &name, const std::string &key,
                                  const nlohmann::json &value)
    {
        auto row = recorded_position();
        row[key] = value;
        expect_rejected(name, [&] { (void)detail::parse_data_position(row); });
    }

    void test_recorded_positions()
    {
        const auto page = detail::parse_data_position_page(data_fixtures::data_position_page);
        check(page.items.size() == 3 && !page.next_cursor.empty(),
              "recorded position page must parse with a cursor");
        const auto &open = page.items.at(1);
        expect_equal("position wallet", open.wallet, "0x2222222222222222222222222222222222222222");
        check(open.condition_id.starts_with("0x276b505d") && open.status == "OPEN" &&
                  near(open.current_size, 13774.944) && near(open.realized_pnl, 2257.8238) &&
                  open.event_id == "1202539" && open.outcome_index == 0 &&
                  open.end_date == "2026-10-18" && open.first_entry_at == 1792261224 &&
                  !open.opposite_asset_id.empty() && !open.redeemable && !open.verified,
              "recorded open position fields must parse");
        const auto &closed = page.items.at(2);
        check(closed.status == "CLOSED" && closed.current_size == 0.0 &&
                  near(closed.realized_pnl, 155819.9983),
              "recorded closed position must parse");
        check(open.raw.contains("percent_realized_pnl"), "position raw must keep the row");
    }

    void test_position_variants()
    {
        auto row = recorded_position();
        row["event_id"] = 0;
        row["end_date"] = "1970-01-01";
        row["outcome_index"] = 999;
        row["last_event_at"] = "0";
        row["first_entry_at"] = nullptr;
        row["opposite_outcome"] = "";
        row["current_size"] = "12.5";
        const auto position = detail::parse_data_position(row);
        check(position.event_id.empty() && position.end_date.empty() && !position.outcome_index &&
                  !position.last_event_at && !position.first_entry_at &&
                  position.opposite_outcome.empty() && position.current_size == 12.5,
              "Data API placeholders must read as unset");
        row["event_id"] = 77;
        check(detail::parse_data_position(row).event_id == "77",
              "an integer event ID must read as text");
    }

    void test_portfolio_reads()
    {
        const auto value = detail::parse_data_portfolio_value(data_fixtures::data_portfolio_value);
        check(value.wallet == "0x2222222222222222222222222222222222222222" &&
                  near(value.value, 45483.5095),
              "portfolio value must parse");

        const auto stats = detail::parse_data_user_stats(data_fixtures::data_user_stats);
        check(stats && stats->traded_market_count == 2795 &&
                  near(stats->biggest_win, 137934.308878) && stats->views == 4356 &&
                  stats->join_date == 1782617410 && stats->all_time_pnl,
              "user stats must parse");
        const auto &pnl = *stats->all_time_pnl;
        check(pnl.timestamp == 1792273655 && pnl.source_block == 95712205 &&
                  pnl.trade_count == 6287816 && near(pnl.realized_pnl, -79091.535325) &&
                  near(pnl.unrealized_pnl, -23284.365597) && near(pnl.fees_paid, -240924.82636) &&
                  !pnl.deposits && !pnl.cashflow_net,
              "all-time PnL must keep missing amounts unset");

        const auto empty = detail::parse_data_user_stats(
            R"({"data":{"proxy_wallet":"0x1","trades":0,"biggest_win":0.0,"views":0,"join_date":null,"all_time_pnl":null}})");
        check(empty && !empty->join_date && !empty->all_time_pnl,
              "a wallet without history must have no PnL");
        check(!detail::parse_data_user_stats(R"({"data":null})"),
              "null stats must read as no stats");

        const auto series = detail::parse_data_pnl_series(data_fixtures::data_pnl_series);
        check(series.interval == "1d" && series.fidelity == "1h" &&
                  series.source_fidelity == "1d" && series.points.size() == 2 &&
                  series.points.at(0).timestamp == 1792191600 &&
                  series.points.at(1).timestamp == 1792195200,
              "PnL series must parse");

        const auto volume = detail::parse_data_user_volume(data_fixtures::data_user_volume);
        check(near(volume.volume, 68137252.68586) && near(volume.volume_usdc, 18235127.828172) &&
                  volume.trade_count == 6359523,
              "user volume must parse");
    }

    void test_malformed_input()
    {
        expect_position_rejected("an unknown position status", "status", "MERGEABLE");
        expect_position_rejected("a string boolean", "redeemable", "false");
        expect_position_rejected("a missing boolean", "verified", nullptr);
        expect_position_rejected("a missing amount", "total_pnl", nullptr);
        expect_position_rejected("a non-hex condition", "condition_id", "abc");
        expect_position_rejected("a malformed end date", "end_date", "18/10/2026");
        expect_position_rejected("a fractional event ID", "event_id", 1.5);
        expect_rejected("a value without data",
                        [] { (void)detail::parse_data_portfolio_value(R"({"value":1})"); });
        expect_rejected("a list value",
                        [] { (void)detail::parse_data_portfolio_value(R"({"data":[]})"); });
        expect_rejected(
            "an unknown PnL fidelity",
            []
            {
                (void)detail::parse_data_pnl_series(
                    R"({"data":{"proxy_wallet":"0x1","interval":"1d","fidelity":"5m","source_fidelity":"1d","points":[]}})");
            });
        expect_rejected(
            "PnL without points",
            []
            {
                (void)detail::parse_data_pnl_series(
                    R"({"data":{"proxy_wallet":"0x1","interval":"1d","fidelity":"1h","source_fidelity":"1d"}})");
            });
        expect_rejected("a fractional trade count",
                        []
                        {
                            (void)detail::parse_data_user_volume(
                                R"({"data":{"volume":1,"volume_usdc":1,"trade_count":1.5}})");
                        });
        expect_rejected(
            "stats with a malformed PnL",
            []
            {
                (void)detail::parse_data_user_stats(
                    R"({"data":{"proxy_wallet":"0x1","trades":0,"biggest_win":0,"views":0,"all_time_pnl":{"timestamp":1}}})");
            });
    }
} // namespace

int main()
{
    test_recorded_positions();
    test_position_variants();
    test_portfolio_reads();
    test_malformed_input();
    return check_support::finish("test_data_portfolio_parsing");
}
