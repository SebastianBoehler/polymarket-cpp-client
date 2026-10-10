#include "../src/clob_client_test_fixture.hpp"
#include "check_support.hpp"
#include "order_execution.hpp"

#include "polymarket/market_price.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

using namespace polymarket;
using check_support::check;

namespace
{
    // Best price first, as OrderbookManager stores books.
    Orderbook stream_book()
    {
        Orderbook book;
        book.asset_id = "123";
        book.asks = {{0.5, 2.0}, {0.6, 3.0}, {0.7, 1.0}};
        book.bids = {{0.5, 2.0}, {0.4, 3.0}, {0.3, 1.0}};
        return book;
    }

    // Best price last, as the REST /book endpoint returns books.
    Orderbook rest_book()
    {
        auto book = stream_book();
        std::reverse(book.asks.begin(), book.asks.end());
        std::reverse(book.bids.begin(), book.bids.end());
        return book;
    }

    Orderbook shuffled_book()
    {
        auto book = stream_book();
        std::swap(book.asks[0], book.asks[1]);
        std::swap(book.bids[1], book.bids[2]);
        return book;
    }

    bool same_estimate(const MarketPriceEstimate &left, const MarketPriceEstimate &right)
    {
        return left.price == right.price && left.average_price == right.average_price &&
               left.share_units == right.share_units &&
               left.collateral_units == right.collateral_units && left.levels == right.levels &&
               left.fully_fillable == right.fully_fillable;
    }

    void test_buy_simulates_fill_across_levels()
    {
        const auto estimate = estimate_market_price(stream_book(), OrderSide::BUY, 2.0, "0.1");
        check(estimate.ok(), "BUY estimate must succeed");
        if (!estimate) return;
        const auto &value = estimate.value();
        // The signed budget at 0.6 is 1999998; 0.5 takes 1000000 for 2 shares and
        // the rest buys 1.666663 shares at 0.6 for 999998.
        check(value.price == 0.6, "BUY limit must be the first level that covers the budget");
        check(value.share_units == 3'666'663, "BUY shares mismatch");
        check(value.collateral_units == 1'999'998, "BUY collateral mismatch");
        check(value.levels == 2, "BUY must touch two levels");
        check(value.fully_fillable, "BUY must be fully fillable");
        check(std::abs(value.average_price - 1'999'998.0 / 3'666'663.0) < 1e-15,
              "BUY average price mismatch");
    }

    void test_sell_simulates_fill_across_levels()
    {
        const auto estimate = estimate_market_price(stream_book(), OrderSide::SELL, 4.0, "0.1");
        check(estimate.ok(), "SELL estimate must succeed");
        if (!estimate) return;
        const auto &value = estimate.value();
        check(value.price == 0.4, "SELL limit must be the first level that covers the shares");
        check(value.share_units == 4'000'000, "SELL shares mismatch");
        check(value.collateral_units == 1'800'000, "SELL collateral mismatch");
        check(value.levels == 2, "SELL must touch two levels");
        check(value.average_price == 0.45, "SELL average price mismatch");
    }

    void test_book_order_does_not_change_estimate()
    {
        for (const auto side : {OrderSide::BUY, OrderSide::SELL})
        {
            for (const double amount : {0.5, 2.0, 4.0, 9.0})
            {
                const auto stream =
                    estimate_market_price(stream_book(), side, amount, "0.1", OrderType::FAK);
                const auto rest =
                    estimate_market_price(rest_book(), side, amount, "0.1", OrderType::FAK);
                const auto shuffled =
                    estimate_market_price(shuffled_book(), side, amount, "0.1", OrderType::FAK);
                const auto label =
                    std::string(side == OrderSide::BUY ? "BUY " : "SELL ") + std::to_string(amount);
                check(stream && rest && shuffled, label + " estimates must succeed");
                if (!stream || !rest || !shuffled) continue;
                check(same_estimate(stream.value(), rest.value()) &&
                          same_estimate(stream.value(), shuffled.value()),
                      label + " must not depend on level order");
                check(stream.value().price == detail::calculate_market_price(stream_book(), side,
                                                                             amount, OrderType::FAK,
                                                                             "0.1"),
                      label + " must match the price create_market_order signs");
            }
        }
        // A best-first book used to be walked from its worst level.
        check(detail::calculate_market_price(stream_book(), OrderSide::BUY, 0.5, OrderType::FOK,
                                             "0.1") == 0.5,
              "best-first asks must be walked from the lowest price");
        check(detail::calculate_market_price(stream_book(), OrderSide::SELL, 1.0, OrderType::FOK,
                                             "0.1") == 0.5,
              "best-first bids must be walked from the highest price");
    }

    void test_shallow_book()
    {
        const auto fak =
            estimate_market_price(stream_book(), OrderSide::BUY, 9.0, "0.1", OrderType::FAK);
        check(fak.ok(), "FAK must estimate a partial fill");
        if (fak)
        {
            check(!fak.value().fully_fillable, "FAK partial fill must be reported");
            check(fak.value().price == 0.7, "FAK limit must be the worst level");
            check(fak.value().share_units == 6'000'000 &&
                      fak.value().collateral_units == 3'500'000 && fak.value().levels == 3,
                  "FAK must take every level");
        }

        const auto fok = estimate_market_price(stream_book(), OrderSide::BUY, 9.0, "0.1");
        check(!fok && fok.error().code == SdkErrorCode::InsufficientLiquidity,
              "FOK must reject a shallow book");

        Orderbook empty;
        const auto no_asks =
            estimate_market_price(empty, OrderSide::BUY, 1.0, "0.1", OrderType::FAK);
        check(!no_asks && no_asks.error().code == SdkErrorCode::InsufficientLiquidity,
              "an empty side must be insufficient liquidity");
    }

    void test_invalid_input()
    {
        const auto invalid = [](const Result<MarketPriceEstimate> &result)
        { return !result && result.error().code == SdkErrorCode::InvalidArgument; };
        const auto book = stream_book();
        check(invalid(estimate_market_price(book, OrderSide::BUY, 1.0, "0.1", OrderType::GTC)),
              "limit order types must be rejected");
        check(invalid(estimate_market_price(book, OrderSide::BUY, 0.0, "0.1")),
              "zero amount must be rejected");
        const auto bad_side = estimate_market_price(book, static_cast<OrderSide>(2), 1.0, "0.1");
        check(invalid(bad_side) && bad_side.error().message == "order side must be BUY or SELL",
              "an unknown side must be rejected");
        check(invalid(estimate_market_price(book, OrderSide::BUY,
                                            std::numeric_limits<double>::quiet_NaN(), "0.1")),
              "NaN amount must be rejected");
        check(invalid(estimate_market_price(book, OrderSide::BUY, 1.0, "0.3")),
              "unsupported tick size must be rejected");

        auto off_tick = book;
        off_tick.asks[0].price = 0.55;
        check(invalid(estimate_market_price(off_tick, OrderSide::BUY, 0.5, "0.1")),
              "a level off the tick grid must be rejected");

        auto nan_price = shuffled_book();
        nan_price.asks[2].price = std::numeric_limits<double>::quiet_NaN();
        check(invalid(estimate_market_price(nan_price, OrderSide::BUY, 0.5, "0.1")),
              "a NaN price must be rejected before ordering levels");
    }

    void test_client_fetches_tick_and_book()
    {
        clob_test::LocalServer server;
        ClobClient client(server.url(), 137);
        server.enqueue(R"({"minimum_tick_size":"0.1"})");
        server.enqueue(
            R"({"asset_id":"123","bids":[],"asks":[{"price":"0.7","size":"1"},{"price":"0.6","size":"3"},{"price":"0.5","size":"2"}]})");
        const auto estimate = client.estimate_market_price("123", OrderSide::BUY, 2.0);
        server.enqueue(R"({"error":"no tick"})", 500);
        const auto missing_tick = client.estimate_market_price("456", OrderSide::BUY, 2.0);
        const auto rejected =
            client.estimate_market_price("123", OrderSide::BUY, 2.0, OrderType::GTD);
        const auto bad_side = client.estimate_market_price("123", static_cast<OrderSide>(2), 2.0);
        const auto requests = server.requests();

        check(estimate && estimate.value().price == 0.6 &&
                  estimate.value().share_units == 3'666'663,
              "client estimate must walk the REST book");
        check(!missing_tick && missing_tick.error().code == SdkErrorCode::ApiResponse &&
                  missing_tick.error().http_status == 500 && missing_tick.error().retryable &&
                  missing_tick.error().endpoint == "/tick-size",
              "a tick size server error must keep its HTTP status");
        check(!rejected && rejected.error().code == SdkErrorCode::InvalidArgument,
              "client must reject limit order types");
        check(!bad_side && bad_side.error().code == SdkErrorCode::InvalidArgument,
              "client must reject an unknown side");
        check(requests.size() == 3 && requests[0].target == "/tick-size?token_id=123" &&
                  requests[1].target == "/book?token_id=123" &&
                  requests[2].target == "/tick-size?token_id=456",
              "client must fetch tick then book, stop on tick failure, and validate before I/O");
    }

    void test_client_preserves_book_errors()
    {
        clob_test::LocalServer server;
        ClobClient client(server.url(), 137);
        server.enqueue(R"({"minimum_tick_size":"0.1"})");
        server.enqueue(R"({"error":"No orderbook exists for the requested token id"})", 404);
        const auto missing = client.estimate_market_price("123", OrderSide::BUY, 2.0);
        server.enqueue(R"({"asset_id":"123","bids":[)");
        const auto malformed = client.estimate_market_price("123", OrderSide::BUY, 2.0);
        server.enqueue(R"({"minimum_tick_size":null})");
        const auto no_tick = client.estimate_market_price("456", OrderSide::BUY, 2.0);

        check(!missing && missing.error().code == SdkErrorCode::ApiResponse &&
                  missing.error().http_status == 404 && !missing.error().retryable &&
                  missing.error().endpoint == "/book" &&
                  missing.error().response_body_excerpt.find("No orderbook") != std::string::npos,
              "a book 404 must surface as a non-retryable API rejection");
        check(!malformed && malformed.error().code == SdkErrorCode::Parse &&
                  !malformed.error().retryable && malformed.error().endpoint == "/book",
              "a malformed book must surface as a parse failure");
        check(!no_tick && no_tick.error().code == SdkErrorCode::Parse &&
                  no_tick.error().endpoint == "/tick-size",
              "a tick size response without a value must be a parse failure");
    }
} // namespace

int main()
{
    test_buy_simulates_fill_across_levels();
    test_sell_simulates_fill_across_levels();
    test_book_order_does_not_change_estimate();
    test_shallow_book();
    test_invalid_input();
    test_client_fetches_tick_and_book();
    test_client_preserves_book_errors();
    return check_support::finish("test_market_price");
}
