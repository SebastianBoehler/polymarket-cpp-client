#include "../src/clob_client_test_fixture.hpp"
#include "check_support.hpp"
#include "order_placement_test_support.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

using namespace polymarket;
using check_support::check;

namespace
{
    constexpr const char *accepted_order =
        R"({"success":true,"orderID":"order-1","status":"matched"})";
    // REST order: best ask last.
    constexpr const char *ask_book =
        R"({"asset_id":"123","bids":[],"asks":[{"price":"0.70","size":"1"},{"price":"0.60","size":"3"},{"price":"0.50","size":"2"}]})";

    PlaceMarketOrderParams market_order(OrderSide side, double amount)
    {
        PlaceMarketOrderParams params;
        params.token_id = "123";
        params.side = side;
        params.amount = amount;
        return params;
    }

    std::vector<std::string> targets(const std::vector<clob_test::Request> &requests)
    {
        std::vector<std::string> result;
        result.reserve(requests.size());
        for (const auto &request : requests)
            result.push_back(request.method + " " + request.target);
        return result;
    }

    nlohmann::json order_body(const clob_test::Request &request)
    {
        return nlohmann::json::parse(request.body);
    }

    void test_book_walk_prices_unbounded_orders()
    {
        clob_test::LocalServer server;
        auto client = clob_test::authenticated_client(server.url());
        server.enqueue(R"({"minimum_tick_size":"0.01"})");
        server.enqueue(ask_book);
        server.enqueue(R"({"neg_risk":false})");
        server.enqueue(accepted_order);
        const auto placed = client.place_market_order(market_order(OrderSide::BUY, 2.0));

        auto fok = market_order(OrderSide::BUY, 9.0);
        fok.order_type = OrderType::FOK;
        server.enqueue(ask_book);
        const auto shallow = client.place_market_order(fok);
        const auto requests = server.requests();

        check(placed.ok(), "a BUY the book can fill must be posted");
        check(!shallow && shallow.error().code == SdkErrorCode::InsufficientLiquidity,
              "a FOK the book cannot fill must fail before signing");
        check(targets(requests) ==
                  std::vector<std::string>{"GET /tick-size?token_id=123", "GET /book?token_id=123",
                                           "GET /neg-risk?token_id=123", "POST /order",
                                           "GET /book?token_id=123"},
              "unbounded orders must fetch tick and book, and never post a shallow FOK");
        if (requests.size() != 5) return;
        const auto body = order_body(requests[3]);
        // Signed at the 0.60 level: the same ratio create_market_order produces.
        check(body["orderType"] == "FAK" && body["order"]["makerAmount"] == "1999998" &&
                  body["order"]["takerAmount"] == "3333330",
              "unbounded BUY must post FAK at the walked price");
    }

    void test_worst_price_skips_the_book()
    {
        clob_test::LocalServer server;
        auto client = clob_test::authenticated_client(server.url());
        auto buy = market_order(OrderSide::BUY, 2.0);
        buy.worst_price = 0.65;
        buy.order_type = OrderType::FOK;
        server.enqueue(R"({"minimum_tick_size":"0.01"})");
        server.enqueue(R"({"neg_risk":false})");
        server.enqueue(accepted_order);
        const auto bounded_buy = client.place_market_order(buy);

        auto sell = market_order(OrderSide::SELL, 5.0);
        sell.worst_price = 0.4;
        server.enqueue(accepted_order);
        const auto bounded_sell = client.place_market_order(sell);
        const auto requests = server.requests();

        check(bounded_buy && bounded_sell, "bounded orders must be posted");
        check(targets(requests) == std::vector<std::string>{"GET /tick-size?token_id=123",
                                                            "GET /neg-risk?token_id=123",
                                                            "POST /order", "POST /order"},
              "bounded orders must not fetch the book");
        if (requests.size() != 4) return;
        const auto buy_body = order_body(requests[2]);
        const auto maker = std::stoull(buy_body["order"]["makerAmount"].get<std::string>());
        const auto taker = std::stoull(buy_body["order"]["takerAmount"].get<std::string>());
        check(buy_body["orderType"] == "FOK" && maker == 1'999'998 && taker == 3'076'920 &&
                  maker * 100 == taker * 65,
              "bounded BUY must sign exactly at worst_price within the budget");
        const auto sell_body = order_body(requests[3]);
        check(sell_body["orderType"] == "FAK" && sell_body["order"]["makerAmount"] == "5000000" &&
                  sell_body["order"]["takerAmount"] == "2000000",
              "bounded SELL must sign the shares at worst_price");
    }

    void test_stale_tick_refreshes_once()
    {
        clob_test::LocalServer server;
        auto client = clob_test::authenticated_client(server.url());
        auto bounded = market_order(OrderSide::BUY, 1.0);
        bounded.worst_price = 0.965;
        bounded.neg_risk = false;
        server.enqueue(R"({"minimum_tick_size":"0.01"})");
        server.enqueue(R"({"minimum_tick_size":"0.001"})");
        server.enqueue(accepted_order);
        const auto refreshed_bound = client.place_market_order(bounded);

        // The book already rests on a 0.0001 grid the fetched 0.01 tick cannot represent.
        server.enqueue(R"({"minimum_tick_size":"0.01"})");
        server.enqueue(R"({"asset_id":"456","bids":[],"asks":[{"price":"0.9655","size":"10"}]})");
        server.enqueue(R"({"minimum_tick_size":"0.0001"})");
        auto unbounded = market_order(OrderSide::BUY, 1.0);
        unbounded.token_id = "456";
        unbounded.neg_risk = false;
        server.enqueue(accepted_order);
        const auto refreshed_walk = client.place_market_order(unbounded);
        const auto requests = server.requests();

        check(refreshed_bound.ok(), "a bound on the refreshed grid must be accepted");
        check(refreshed_walk.ok(), "a book walk on the refreshed grid must be accepted");
        check(targets(requests) ==
                  std::vector<std::string>{"GET /tick-size?token_id=123",
                                           "GET /tick-size?token_id=123", "POST /order",
                                           "GET /tick-size?token_id=456", "GET /book?token_id=456",
                                           "GET /tick-size?token_id=456", "POST /order"},
              "a stale tick must be refetched once before signing");
    }

    void test_preserves_metadata_and_book_errors()
    {
        clob_test::LocalServer server;
        auto client = clob_test::authenticated_client(server.url());
        server.enqueue(R"({"minimum_tick_size":"0.01"})");
        server.enqueue(R"({"error":"No orderbook exists for the requested token id"})", 404);
        const auto missing_book = client.place_market_order(market_order(OrderSide::BUY, 2.0));
        server.enqueue("not json");
        const auto malformed_book = client.place_market_order(market_order(OrderSide::BUY, 2.0));
        auto other = market_order(OrderSide::BUY, 2.0);
        other.token_id = "456";
        server.enqueue("not json");
        const auto malformed_tick = client.place_market_order(other);

        check(!missing_book && missing_book.error().code == SdkErrorCode::ApiResponse &&
                  missing_book.error().http_status == 404 && !missing_book.error().retryable &&
                  !missing_book.error().response_body_excerpt.empty(),
              "a book 404 must surface as a non-retryable API rejection");
        check(!malformed_book && malformed_book.error().code == SdkErrorCode::Parse &&
                  !malformed_book.error().retryable,
              "a malformed book must surface as a parse failure");
        check(!malformed_tick && malformed_tick.error().code == SdkErrorCode::Parse &&
                  malformed_tick.error().endpoint == "/tick-size",
              "a malformed tick size must surface as a parse failure");
        check(server.requests().size() == 4, "lookup failures must stop before posting");
    }

    void test_preserves_metadata_errors_while_signing()
    {
        using namespace order_placement_test;
        for (const auto branch : metadata_branches)
            for (const auto &failure : metadata_failures)
            {
                clob_test::LocalServer server;
                auto client = clob_test::authenticated_client(server.url());
                auto params = market_order(OrderSide::BUY, 1.0);
                params.worst_price = 0.5;
                params.neg_risk = false;
                int warmup_posts = 0;
                if (branch == MetadataBranch::TickRefresh)
                {
                    // Caches 0.01, so a 0.965 bound forces a refetch inside order creation.
                    server.enqueue(R"({"minimum_tick_size":"0.01"})");
                    server.enqueue(accepted_order);
                    (void)client.place_market_order(params);
                    params.worst_price = 0.965;
                    warmup_posts = 1;
                }
                else if (branch == MetadataBranch::ExplicitTick)
                {
                    params.tick_size = "0.01";
                }
                else
                {
                    server.enqueue(R"({"minimum_tick_size":"0.01"})");
                    params.neg_risk.reset();
                }
                server.enqueue(failure.body, failure.status);
                const auto placed = client.place_market_order(params);

                check_preserved_error(placed, branch, failure, "place_market_order");
                int posts = 0;
                for (const auto &request : server.requests())
                    posts += request.target == "/order";
                check(posts == warmup_posts, "a failed metadata lookup must not post the order");
            }
    }

    void test_rejects_before_any_request()
    {
        clob_test::LocalServer server;
        auto client = clob_test::authenticated_client(server.url());
        const auto invalid = [](const Result<OrderResponse> &result)
        { return !result && result.error().code == SdkErrorCode::InvalidArgument; };

        auto gtc = market_order(OrderSide::BUY, 1.0);
        gtc.order_type = OrderType::GTC;
        check(invalid(client.place_market_order(gtc)), "resting order types must be rejected");
        check(invalid(client.place_market_order(market_order(OrderSide::SELL, 0.0))),
              "zero amount must be rejected");
        auto bad_bound = market_order(OrderSide::BUY, 1.0);
        bad_bound.worst_price = 1.0;
        check(invalid(client.place_market_order(bad_bound)), "worst price 1 must be rejected");
        auto no_token = market_order(OrderSide::BUY, 1.0);
        no_token.token_id.clear();
        check(invalid(client.place_market_order(no_token)), "empty token_id must be rejected");

        ClobClient public_client(server.url(), 137);
        const auto unauthenticated =
            public_client.place_market_order(market_order(OrderSide::BUY, 1.0));
        check(!unauthenticated && unauthenticated.error().code == SdkErrorCode::Auth,
              "a public client must not place orders");
        check(server.requests().empty(), "invalid orders must not reach the server");
    }
} // namespace

int main()
{
    test_book_walk_prices_unbounded_orders();
    test_worst_price_skips_the_book();
    test_stale_tick_refreshes_once();
    test_preserves_metadata_and_book_errors();
    test_preserves_metadata_errors_while_signing();
    test_rejects_before_any_request();
    return check_support::finish("test_market_order_placement");
}
