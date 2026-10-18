#include "check_support.hpp"
#include "data_fixtures.hpp"
#include "data_parsing.hpp"

#include <cmath>
#include <functional>
#include <string>

namespace
{
    using check_support::check;
    using check_support::expect_equal;
    namespace detail = polymarket::detail;

    bool near(const std::optional<double> &value, double expected)
    {
        return value && std::abs(*value - expected) < 1e-9 * std::max(1.0, std::abs(expected));
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

    std::string page_of(const std::string &rows, bool has_more = false,
                        const std::string &cursor = "null")
    {
        return R"({"data":[)" + rows + R"(],"pagination":{"has_more":)" +
               (has_more ? "true" : "false") + R"(,"next_cursor":)" + cursor + "}}";
    }

    const std::string condition =
        "0xefa17dee3af09f69f9ddf245b969aa4efbe7c71cdf06ee49d694408bc33e2ed2";

    std::string trade_row(const std::string &overrides)
    {
        return R"({"proxy_wallet":"0x1111111111111111111111111111111111111111","token_id":"7","condition_id":")" +
               condition +
               R"(","side":"BUY","size":5,"price":0.5,"timestamp":1792266921,"transaction_hash":"0xab")" +
               overrides + "}";
    }

    void test_recorded_trades()
    {
        const auto page = detail::parse_data_trade_page(data_fixtures::data_trade_page);
        check(page.items.size() == 3 && !page.next_cursor.empty(),
              "recorded trade page must parse with a cursor");
        const auto &trade = page.items.at(0);
        expect_equal("trade wallet", trade.wallet, "0x1111111111111111111111111111111111111111");
        expect_equal("trade condition", trade.condition_id, condition);
        check(trade.side == "BUY" && trade.size == 5.0 && trade.price == 0.002 &&
                  trade.timestamp == 1792266921 && trade.outcome_index == 0 &&
                  trade.asset_id.starts_with("3333979837291603722"),
              "recorded trade fields must parse");
        check(trade.raw.contains("profile_image_optimized"), "trade raw must keep the row");
    }

    void test_recorded_activity()
    {
        const auto page = detail::parse_data_activity_page(data_fixtures::data_activity_page);
        check(page.items.size() == 10, "recorded activity page must hold one row per type");
        const auto &trade = page.items.at(0);
        check(trade.type == "TRADE" && trade.known && trade.side == "BUY" &&
                  near(trade.shares, 5.39) && near(trade.amount, 0.4312) &&
                  near(trade.price, 0.08) && trade.outcome_index == 0 &&
                  trade.asset_id.starts_with("10370414") && trade.timestamp == 1792265849,
              "a trade activity must carry its trade fields");
        const auto &merge = page.items.at(1);
        check(merge.type == "MERGE" && near(merge.amount, 5.4) && !merge.shares && !merge.price &&
                  merge.side.empty() && !merge.outcome_index &&
                  merge.condition_id.starts_with("0xada24d47"),
              "a market event must carry its condition and amount only");
        const auto &deposit = page.items.at(4);
        check(deposit.type == "DEPOSIT" && near(deposit.amount, 50000.0) &&
                  deposit.condition_id.empty() && !deposit.shares && deposit.title.empty(),
              "an account credit must carry its amount only");
        bool all_known = true;
        for (const auto &row : page.items)
            all_known = all_known && row.known && row.timestamp.has_value() &&
                        !row.transaction_hash.empty();
        check(all_known, "recorded activity kinds must all be known");

        const auto last = detail::parse_data_activity_page(data_fixtures::data_activity_last_page);
        check(last.items.size() == 1 && last.next_cursor.empty(), "a final page must end paging");
    }

    void test_activity_variants()
    {
        const auto unknown = detail::parse_data_activity(nlohmann::json::parse(
            R"({"type":"NEW_KIND","proxy_wallet":"0x1","timestamp":0,"extra":1})"));
        check(!unknown.known && unknown.type == "NEW_KIND" && !unknown.timestamp &&
                  unknown.wallet == "0x1" && unknown.raw.at("extra") == 1,
              "a newer activity kind must keep its raw row");
        const auto untyped = detail::parse_data_activity(nlohmann::json::parse(R"({"type":5})"));
        check(!untyped.known && untyped.type.empty(),
              "a non-string type must read as an unknown kind");

        const auto combo = detail::parse_data_activity(nlohmann::json::parse(
            R"({"type":"TRADE","is_combo":true,"proxy_wallet":"0x1","timestamp":"1792265849","transaction_hash":"0xab",)"
            R"("condition_id":"0x03ABCDEF00000000000000000000000000000000000000000000000000000001","token_id":"99",)"
            R"("side":"SELL","size":"2","usdc_size":"1","price":"0.5","outcome":"Yes","outcome_index":1})"));
        check(combo.is_combo &&
                  combo.condition_id ==
                      "0x03abcdef000000000000000000000000000000000000000000000000000000" &&
                  combo.asset_id == "99" && combo.outcome.empty() && !combo.outcome_index &&
                  near(combo.shares, 2.0),
              "a combo trade must normalize its condition ID and drop single-market fields");

        const auto tip = detail::parse_data_activity(nlohmann::json::parse(
            R"({"type":"TIP","proxy_wallet":"0x1","timestamp":1,"transaction_hash":"0xab","usdc_size":3,"side":"IN"})"));
        check(tip.tip_side == "IN" && near(tip.amount, 3.0), "a tip must carry its side");

        const auto none =
            detail::parse_data_trade(nlohmann::json::parse(trade_row(R"(,"outcome_index":999)")));
        check(!none.outcome_index, "outcome index 999 must read as unset");
    }

    void test_malformed_input()
    {
        expect_rejected("has_more without a cursor",
                        [] { (void)detail::parse_data_trade_page(page_of("", true)); });
        expect_rejected("a cursor without has_more",
                        [] { (void)detail::parse_data_trade_page(page_of("", false, R"("x")")); });
        expect_rejected("an empty cursor",
                        [] { (void)detail::parse_data_trade_page(page_of("", true, R"("")")); });
        expect_rejected("missing pagination",
                        [] { (void)detail::parse_data_trade_page(R"({"data":[]})"); });
        expect_rejected("missing next_cursor",
                        []
                        {
                            (void)detail::parse_data_trade_page(
                                R"({"data":[],"pagination":{"has_more":false}})");
                        });
        expect_rejected("a non-boolean has_more",
                        []
                        {
                            (void)detail::parse_data_trade_page(
                                R"({"data":[],"pagination":{"has_more":"no","next_cursor":null}})");
                        });
        expect_rejected(
            "an unknown trade side",
            [] { (void)detail::parse_data_trade_page(page_of(trade_row(R"(,"side":"HOLD")"))); });
        expect_rejected("a non-hex condition",
                        []
                        {
                            (void)detail::parse_data_trade_page(
                                page_of(trade_row(R"(,"condition_id":"abc")")));
                        });
        expect_rejected(
            "a boolean size",
            [] { (void)detail::parse_data_trade_page(page_of(trade_row(R"(,"size":true)"))); });
        expect_rejected(
            "a timestamp past 9999", []
            { (void)detail::parse_data_trade_page(page_of(trade_row(R"(,"timestamp":1e15)"))); });
        expect_rejected("a trade without a hash",
                        []
                        {
                            (void)detail::parse_data_trade(nlohmann::json::parse(
                                R"({"proxy_wallet":"0x1","token_id":"7","side":"BUY"})"));
                        });
        expect_rejected(
            "a combo trade with a market condition",
            []
            {
                (void)detail::parse_data_activity(nlohmann::json::parse(
                    R"({"type":"TRADE","is_combo":true,"proxy_wallet":"0x1","timestamp":1,"transaction_hash":"0xab",)"
                    R"("condition_id":")" +
                    condition +
                    R"(","token_id":"9","side":"BUY","size":1,"usdc_size":1,"price":1})"));
            });
        expect_rejected(
            "a known kind without an amount",
            []
            {
                (void)detail::parse_data_activity(nlohmann::json::parse(
                    R"({"type":"DEPOSIT","proxy_wallet":"0x1","timestamp":1,"transaction_hash":"0xab"})"));
            });
        expect_rejected(
            "an unknown tip side",
            []
            {
                (void)detail::parse_data_activity(nlohmann::json::parse(
                    R"({"type":"TIP","proxy_wallet":"0x1","timestamp":1,"transaction_hash":"0xab","usdc_size":1,"side":"UP"})"));
            });
    }
} // namespace

int main()
{
    test_recorded_trades();
    test_recorded_activity();
    test_activity_variants();
    test_malformed_input();
    return check_support::finish("test_data_parsing");
}
