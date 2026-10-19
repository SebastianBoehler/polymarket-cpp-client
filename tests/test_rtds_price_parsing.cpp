#include "check_support.hpp"
#include "rtds_parsing.hpp"
#include "rtds_price_fixtures.hpp"

#include <cmath>
#include <functional>
#include <stdexcept>
#include <string>

namespace
{
    using check_support::check;
    using check_support::expect_equal;
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

    polymarket::RtdsPriceEvent price(const std::string &frame)
    {
        return *detail::parse_rtds_message(frame)->price;
    }

    std::string with_payload(const char *frame, const char *key, const nlohmann::json &value)
    {
        auto parsed = nlohmann::json::parse(frame);
        parsed["payload"][key] = value;
        return parsed.dump();
    }

    void test_scaling()
    {
        expect_equal("whole", detail::scaled_e18_decimal("81529465877486595000000"),
                     "81529.465877486595");
        expect_equal("integer", detail::scaled_e18_decimal("2000000000000000000"), "2");
        expect_equal("fraction", detail::scaled_e18_decimal("12345"), "0.000000000000012345");
        expect_equal("negative", detail::scaled_e18_decimal("-1500000000000000000"), "-1.5");
        expect_equal("zero", detail::scaled_e18_decimal("-000"), "0");
        expect_rejected("a decimal input", [] { (void)detail::scaled_e18_decimal("1.5"); });
        expect_rejected("an empty input", [] { (void)detail::scaled_e18_decimal("-"); });
    }

    void test_recorded_crypto()
    {
        const auto binance = price(rtds_fixtures::rtds_binance_update);
        check(binance.topic == "prices.crypto.binance" && binance.type == "update" &&
                  binance.symbol == "btcusdt" && binance.price.timestamp_ms == 1792287281000 &&
                  binance.price.exact_value == "81633.21000000" &&
                  binance.price.value == 81633.21 && binance.timestamp_ms == 1792287281124 &&
                  !binance.window_seconds,
              "a recorded Binance price must parse");

        const auto chainlink = price(rtds_fixtures::rtds_chainlink_update);
        check(chainlink.topic == "prices.crypto.chainlink" && chainlink.symbol == "btc/usd" &&
                  chainlink.price.exact_value == "81529.465877486595" &&
                  std::abs(chainlink.price.value - 81529.4658774866) < 1e-6,
              "a Chainlink price must scale its full-accuracy value");

        const auto thirty = price(rtds_fixtures::rtds_twap_thirty_update);
        const auto sixty = price(rtds_fixtures::rtds_twap_sixty_update);
        check(thirty.topic == "prices.crypto.chainlink.twap" && thirty.window_seconds == 30 &&
                  thirty.price.exact_value == "81529.202885321459499008" &&
                  sixty.window_seconds == 60 &&
                  sixty.price.exact_value == "81527.437376226865446912",
              "TWAPs must carry their window and scaled value");

        const auto fallback =
            price(with_payload(rtds_fixtures::rtds_chainlink_update, "full_accuracy_value", "n/a"));
        check(fallback.price.exact_value == "81529.4658774866",
              "an unusable full-accuracy value must fall back to value");
    }

    void test_equity()
    {
        const auto update = price(rtds_fixtures::rtds_equity_update);
        check(update.topic == "prices.equity.pyth" && update.type == "update" &&
                  update.symbol == "AAPL" && update.price.exact_value == "231.4215" &&
                  update.received_at == 1792330200150 && update.is_carried_forward == false &&
                  update.history.empty(),
              "an equity update must prefer its full-accuracy value");
        const auto history = price(rtds_fixtures::rtds_equity_history);
        check(history.type == "subscribe" && history.history.size() == 2 &&
                  history.history[0].timestamp_ms == 1792330140000 &&
                  history.history[1].exact_value == "231.35" && history.price.exact_value.empty(),
              "an equity snapshot must carry its history");
    }

    void test_matching()
    {
        const auto binance = price(rtds_fixtures::rtds_binance_update);
        const auto chainlink = price(rtds_fixtures::rtds_chainlink_update);
        const auto thirty = price(rtds_fixtures::rtds_twap_thirty_update);
        const auto equity = price(rtds_fixtures::rtds_equity_update);
        const auto history = price(rtds_fixtures::rtds_equity_history);

        const auto all_binance = detail::rtds_crypto_price_match({"binance", {}});
        const auto eth_binance = detail::rtds_crypto_price_match({"binance", {"ethusdt"}});
        check(detail::rtds_price_matches(all_binance, binance) &&
                  !detail::rtds_price_matches(all_binance, chainlink) &&
                  !detail::rtds_price_matches(eth_binance, binance),
              "crypto matches must check the source and symbols");

        const auto twap_thirty = detail::rtds_crypto_twap_match({30, {"btc/usd"}});
        const auto twap_sixty = detail::rtds_crypto_twap_match({60, {}});
        check(detail::rtds_price_matches(twap_thirty, thirty) &&
                  !detail::rtds_price_matches(twap_sixty, thirty) &&
                  !detail::rtds_price_matches(twap_thirty, chainlink),
              "TWAP matches must check the window");

        const auto aapl = detail::rtds_equity_price_match({"aapl", {}});
        const auto aapl_updates = detail::rtds_equity_price_match({"Aapl", {"update", "update"}});
        check(
            detail::rtds_price_matches(aapl, equity) && detail::rtds_price_matches(aapl, history) &&
                detail::rtds_price_matches(aapl_updates, equity) &&
                !detail::rtds_price_matches(aapl_updates, history) &&
                aapl_updates.types.size() == 1 &&
                !detail::rtds_price_matches(detail::rtds_equity_price_match({"msft", {}}), equity),
            "equity matches must compare symbols case-insensitively and check types");

        const auto keys = detail::rtds_price_keys(twap_thirty);
        check(keys.size() == 1 && keys[0].topic == "crypto_prices_twap_thirty" &&
                  keys[0].type == "update" && keys[0].filters.empty() &&
                  detail::rtds_price_keys(aapl)[0].topic == "equity_prices",
              "price keys must subscribe whole topics");
    }

    void test_rejections()
    {
        expect_rejected("an unknown source",
                        [] { (void)detail::rtds_crypto_price_match({"coinbase", {}}); });
        expect_rejected("an empty symbol",
                        [] { (void)detail::rtds_crypto_price_match({"binance", {""}}); });
        expect_rejected("a 45-second window",
                        [] { (void)detail::rtds_crypto_twap_match({45, {}}); });
        expect_rejected("an empty equity symbol",
                        [] { (void)detail::rtds_equity_price_match({"", {}}); });
        expect_rejected("an unknown equity type",
                        [] { (void)detail::rtds_equity_price_match({"aapl", {"snapshot"}}); });

        expect_rejected(
            "a TWAP with the wrong window", []
            { (void)price(with_payload(rtds_fixtures::rtds_twap_thirty_update, "window_s", 60)); });
        expect_rejected("a TWAP without full accuracy",
                        []
                        {
                            (void)price(with_payload(rtds_fixtures::rtds_twap_sixty_update,
                                                     "full_accuracy_value", nullptr));
                        });
        expect_rejected("a TWAP with decimal full accuracy",
                        []
                        {
                            (void)price(with_payload(rtds_fixtures::rtds_twap_sixty_update,
                                                     "full_accuracy_value", "1.5"));
                        });
        expect_rejected(
            "a price without a value", []
            { (void)price(with_payload(rtds_fixtures::rtds_binance_update, "value", nullptr)); });
        expect_rejected(
            "a fractional timestamp", []
            { (void)price(with_payload(rtds_fixtures::rtds_binance_update, "timestamp", 1.5)); });
        expect_rejected("a crypto snapshot type",
                        []
                        {
                            auto frame = nlohmann::json::parse(rtds_fixtures::rtds_binance_update);
                            frame["type"] = "subscribe";
                            (void)price(frame.dump());
                        });
        expect_rejected(
            "an equity snapshot without data",
            []
            {
                (void)price(
                    R"({"topic":"equity_prices","type":"subscribe","payload":{"symbol":"aapl"}})");
            });
        expect_rejected(
            "an unknown topic", []
            { (void)price(R"({"topic":"activity","type":"trades","payload":{"symbol":"x"}})"); });
    }
} // namespace

int main()
{
    test_scaling();
    test_recorded_crypto();
    test_equity();
    test_matching();
    test_rejections();
    return check_support::finish("test_rtds_price_parsing");
}
