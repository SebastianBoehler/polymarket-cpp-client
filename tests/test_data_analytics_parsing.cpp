#include "check_support.hpp"
#include "data_analytics_fixtures.hpp"
#include "data_parsing.hpp"

#include <cmath>
#include <functional>
#include <string>

namespace
{
    using check_support::check;
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

    nlohmann::json resolution_row()
    {
        return nlohmann::json::parse(data_fixtures::data_resolutions).at("data").at(0);
    }

    void expect_resolution_rejected(const std::string &name, const std::string &key,
                                    const nlohmann::json &value)
    {
        auto row = resolution_row();
        row[key] = value;
        expect_rejected(name, [&] { (void)detail::parse_data_resolution(row); });
    }

    void test_recorded_holders()
    {
        const auto page = detail::parse_data_holders_page(data_fixtures::data_holders_page);
        check(page.items.size() == 2 && !page.next_cursor.empty(),
              "recorded holder page must hold both outcomes with a cursor");
        const auto &group = page.items.at(0);
        check(group.asset_id.starts_with("84586182") && group.holders.size() == 2,
              "holders must group by outcome asset");
        const auto &holder = group.holders.at(0);
        check(holder.asset_id == group.asset_id && near(holder.amount, 8629.618709) &&
                  near(holder.avg_price, 0.8531) && near(holder.total_pnl, 1071.6147) &&
                  holder.outcome_index == 0 && holder.display_username_public && !holder.verified &&
                  holder.name == "Example Holder 1",
              "recorded holder fields must parse");

        auto plain = holder.raw;
        plain.erase("avg_price");
        plain.erase("total_pnl");
        const auto without_pnl = detail::parse_data_holder(plain);
        check(!without_pnl.avg_price && !without_pnl.total_pnl,
              "holders without PnL must leave it unset");
    }

    void test_market_reads()
    {
        const auto interests =
            detail::parse_data_open_interests(data_fixtures::data_open_interests);
        check(interests.size() == 3 && interests.at(0).condition_id.starts_with("0x276b505d") &&
                  near(interests.at(0).value, 23873.989965) &&
                  interests.at(2).condition_id.empty() && interests.at(2).value > 1e8,
              "open interest must read GLOBAL as an empty condition");

        const auto volume = detail::parse_data_live_volume(data_fixtures::data_live_volume);
        check(near(volume.taker_volume_total, 8227726.194698) && volume.markets.size() == 3 &&
                  volume.markets.at(0).condition_id.starts_with("0xf1013717") &&
                  near(volume.markets.at(0).taker_volume, 1950281.899588),
              "live volume must parse its markets");

        const auto prices =
            detail::parse_data_price_history_page(data_fixtures::data_price_history_page);
        check(prices.items.size() == 3 && !prices.next_cursor.empty() &&
                  prices.items.at(0).timestamp == 1792197000 &&
                  near(prices.items.at(0).price, 0.51) &&
                  prices.items.at(0).resolution_seconds == 60,
              "price history must parse");
    }

    void test_recorded_resolutions()
    {
        const auto resolutions = detail::parse_data_resolutions(data_fixtures::data_resolutions);
        check(resolutions.size() == 2, "recorded resolutions must parse");
        const auto &reported = resolutions.at(0);
        check(reported.status == "resolved" && reported.question_id.empty() &&
                  reported.resolved_at == 1792275830 && reported.last_updated_at == 1792275830 &&
                  reported.resolved_block == 95713655 && reported.payouts &&
                  reported.payouts->first == 1.0 && reported.payouts->second == 0.0 &&
                  reported.resolution_source == "reported" && reported.was_arbitrated == false &&
                  reported.transaction_hash.empty() && !reported.log_index && !reported.price,
              "an ISO-timed resolution must parse its payouts");
        const auto &oracle = resolutions.at(1);
        check(!oracle.question_id.empty() && oracle.question_rules_updated &&
                  oracle.last_updated_at == 1786579797 && oracle.log_index == 90 &&
                  near(oracle.price, 1e18) && near(oracle.proposed_price, 1e18) &&
                  !oracle.reproposed_price && !oracle.payouts && oracle.reporter.empty(),
              "an oracle resolution must unset the 69 sentinel");

        auto row = resolution_row();
        row["payouts"] = {"500000", 500000};
        row["log_index"] = 7;
        row["expected_settlement_time"] = 1792300000;
        row["settlement_time_basis"] = "liveness";
        const auto variant = detail::parse_data_resolution(row);
        check(variant.payouts && variant.payouts->first == 0.5 && variant.payouts->second == 0.5 &&
                  variant.log_index == 7 && variant.expected_settlement_time == 1792300000 &&
                  variant.settlement_time_basis == "liveness",
              "resolution variants must parse");
    }

    void test_malformed_input()
    {
        expect_resolution_rejected("an unknown status", "status", "settled");
        expect_resolution_rejected("an unknown reporter", "reporter", "ORACLE");
        expect_resolution_rejected("a single payout", "payouts", nlohmann::json::array({1}));
        expect_resolution_rejected("a malformed time", "resolved_at", "17 Oct 2026");
        expect_resolution_rejected("an offset time", "last_update_timestamp",
                                   "2026-10-17T22:23:50+02:00");
        expect_resolution_rejected("a non-numeric log index", "log_index", "9a");
        expect_resolution_rejected("a missing review flag", "extended_review", nullptr);
        expect_rejected("an envelope object for a list",
                        [] { (void)detail::parse_data_open_interests(R"({"data":{}})"); });
        expect_rejected("a non-hex interest condition",
                        []
                        {
                            (void)detail::parse_data_open_interests(
                                R"({"data":[{"condition_id":"global","value":1}]})");
                        });
        expect_rejected(
            "live volume without markets",
            [] { (void)detail::parse_data_live_volume(R"({"data":{"taker_volume_total":1}})"); });
        expect_rejected(
            "holders without a list",
            []
            {
                (void)detail::parse_data_holders_page(
                    R"({"data":[{"token_id":"1"}],"pagination":{"has_more":false,"next_cursor":null}})");
            });
        expect_rejected(
            "a price without a resolution",
            []
            {
                (void)detail::parse_data_price_history_page(
                    R"({"data":[{"timestamp":1,"price":0.5}],"pagination":{"has_more":false,"next_cursor":null}})");
            });
    }
} // namespace

int main()
{
    test_recorded_holders();
    test_market_reads();
    test_recorded_resolutions();
    test_malformed_input();
    return check_support::finish("test_data_analytics_parsing");
}
