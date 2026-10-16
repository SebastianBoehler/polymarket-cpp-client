#include "check_support.hpp"
#include "gamma_fixtures.hpp"
#include "gamma_parsing.hpp"

#include <cmath>
#include <functional>
#include <stdexcept>
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

    void test_recorded_market_page()
    {
        const auto page = detail::parse_gamma_market_keyset_page(gamma_fixtures::gamma_market_page);
        check(page.items.size() == 2, "recorded market page must hold two markets");
        check(!page.next_cursor.empty(), "recorded market page must carry next_cursor");
        const auto &market = page.items[0];
        expect_equal("market id", market.id, "559651");
        expect_equal("market version", market.version, "v1");
        expect_equal("market slug", market.slug, "xi-jinping-out-before-2027");
        expect_equal("condition id", market.condition_id,
                     "0xa467b14d51f01b957109d9cbb1d6c124fab2a089d52ed8f471d23c2812e743b7");
        expect_equal("yes label", market.yes.label, "Yes");
        expect_equal("no label", market.no.label, "No");
        check(market.yes.token_id.rfind(
                  "32338220190071351435772801779725302244575775216413325951443816017994629993401",
                  0) == 0,
              "yes token id must come from the JSON-encoded clobTokenIds");
        expect_equal("yes position id", market.yes.position_id,
                     "895257453734540292493143124621755605615887197499956109032745153687078305792");
        check(near(market.yes.price, 0.0285) && near(market.no.price, 0.9715),
              "outcome prices must come from the JSON-encoded outcomePrices");
        check(near(market.metrics.volume, 14992362.088046996) &&
                  near(market.metrics.volume_num, 14992362.088046996),
              "volume must parse from text and number forms");
        check(near(market.metrics.liquidity, 502777.00732), "text liquidity must parse");
        check(near(market.prices.best_bid, 0.028), "best bid must parse");
        expect_equal("minimum order size", market.trading.minimum_order_size, "5");
        expect_equal("minimum tick size", market.trading.minimum_tick_size, "0.001");
        check(market.state.neg_risk == false && market.state.active == true,
              "state flags must parse");
        expect_equal("question id", market.resolution.question_id,
                     "0x1d925c6933062c2e38031293612d8680ffa097c5d3ba2f87a8ecc565bd47183e");
        check(market.events.size() == 1 && market.events[0].id == "30828",
              "event references must parse");
        check(market.tags.empty(), "null tags must read as empty");
        check(market.raw.contains("feeSchedule") && market.raw.at("feeSchedule").at("rate") == 0.04,
              "raw must keep unmodeled fields");
        check(page.items[1].yes.position_id.empty(),
              "absent positionIds must leave position ids empty");
    }

    void test_recorded_last_page_and_event()
    {
        const auto last =
            detail::parse_gamma_market_keyset_page(gamma_fixtures::gamma_market_last_page);
        check(last.items.size() == 1 && last.next_cursor.empty(),
              "a page without next_cursor must end paging");

        const auto event = detail::parse_gamma_event_response(gamma_fixtures::gamma_event);
        expect_equal("event id", event.id, "16183");
        expect_equal("event slug", event.slug, "kraken-ipo-in-2025");
        check(event.markets.size() == 2, "event markets must parse");
        check(event.tags.size() == 2 && event.tags[0].label == "Crypto", "event tags must parse");
        check(near(event.metrics.volume, 1618388.549428) && event.metrics.comment_count == 48,
              "event metrics must parse");
        check(event.trading.neg_risk == false, "event trading flags must parse");
        check(event.raw.contains("sortBy"), "event raw must keep unmodeled fields");

        const auto page = detail::parse_gamma_event_keyset_page(gamma_fixtures::gamma_event_page);
        check(page.items.size() == 1 && page.next_cursor.empty(), "event page must parse");

        const auto tags = detail::parse_gamma_tag_references(gamma_fixtures::gamma_tags);
        check(tags.size() == 2 && tags[0].id == "103715" && tags[1].slug == "world-affairs",
              "tag references must parse");
        check(tags[0].raw.at("isCarousel") == false, "tag raw must keep unmodeled fields");
    }

    void test_binary_markets_only()
    {
        const auto page = detail::parse_gamma_market_keyset_page(
            R"({"markets":[{"id":1,"outcomes":["A","B","C"]},{"id":2,"outcomes":["Yes","No"]},{"id":3}]})");
        check(page.items.size() == 1 && page.items[0].id == "2",
              "lists must skip non-binary markets and accept numeric ids");
        expect_rejected("single non-binary market",
                        []
                        {
                            (void)detail::parse_gamma_market_response(
                                R"({"id":"1","outcomes":"[\"A\",\"B\",\"C\"]"})");
                        });
        const auto event = detail::parse_gamma_event_response(
            R"({"id":"e","markets":[{"id":"m1","outcomes":["A","B","C"]},{"id":"m2","outcomes":["Yes","No"]}]})");
        check(event.markets.size() == 1 && event.markets[0].id == "m2",
              "events must skip non-binary markets");
    }

    void test_optional_and_encoded_fields()
    {
        const auto market = detail::parse_gamma_market_response(
            R"({"id":"7","outcomes":["Yes","No"],"positionIds":"[\"11\",\"12\"]","volume":"","conditionId":"","bestBid":null,"gameId":42})");
        check(market.yes.position_id == "11" && market.no.position_id == "12",
              "JSON-encoded positionIds must parse");
        check(!market.metrics.volume && !market.prices.best_bid,
              "empty and null numbers must be unset");
        check(market.condition_id.empty(), "empty conditionId must read as absent");
        expect_equal("numeric game id", market.sports.game_id, "42");
        check(!market.yes.price && market.yes.token_id.empty(),
              "missing outcome data must stay unset");
    }

    void test_malformed_input()
    {
        expect_rejected(
            "missing market id",
            [] { (void)detail::parse_gamma_market_response(R"({"outcomes":["Yes","No"]})"); });
        expect_rejected("bad numeric text",
                        []
                        {
                            (void)detail::parse_gamma_market_response(
                                R"({"id":"1","outcomes":["Yes","No"],"volume":"12abc"})");
                        });
        expect_rejected("boolean number",
                        []
                        {
                            (void)detail::parse_gamma_market_response(
                                R"({"id":"1","outcomes":["Yes","No"],"spread":true})");
                        });
        expect_rejected("non-hex condition id",
                        []
                        {
                            (void)detail::parse_gamma_market_response(
                                R"({"id":"1","outcomes":["Yes","No"],"conditionId":"abc"})");
                        });
        expect_rejected("bad outcome price",
                        []
                        {
                            (void)detail::parse_gamma_market_response(
                                R"({"id":"1","outcomes":["Yes","No"],"outcomePrices":["x","1"]})");
                        });
        expect_rejected(
            "bad encoded list", []
            { (void)detail::parse_gamma_market_response(R"({"id":"1","outcomes":"not json"})"); });
        expect_rejected("string flag",
                        []
                        {
                            (void)detail::parse_gamma_market_response(
                                R"({"id":"1","outcomes":["Yes","No"],"active":"true"})");
                        });
        expect_rejected("empty next cursor",
                        []
                        {
                            (void)detail::parse_gamma_market_keyset_page(
                                R"({"markets":[],"next_cursor":""})");
                        });
        expect_rejected("missing items", []
                        { (void)detail::parse_gamma_event_keyset_page(R"({"next_cursor":"x"})"); });
        expect_rejected("tags object",
                        [] { (void)detail::parse_gamma_tag_references(R"({"id":"1"})"); });
        expect_rejected("event without id",
                        [] { (void)detail::parse_gamma_event_response(R"({"slug":"x"})"); });
    }
} // namespace

int main()
{
    test_recorded_market_page();
    test_recorded_last_page_and_event();
    test_binary_markets_only();
    test_optional_and_encoded_fields();
    test_malformed_input();
    return check_support::finish("test_gamma_parsing");
}
