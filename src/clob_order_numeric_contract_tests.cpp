#include "clob_client_test_fixture.hpp"

#include <cmath>
#include <limits>

namespace clob_test
{
    bool test_public_order_price_grid_validation()
    {
        LocalServer server;
        auto client = authenticated_client(server.url());
        CreateOrderParams params;
        params.token_id = "5005";
        params.price = 0.513;
        params.size = 2.0;
        params.side = OrderSide::BUY;
        params.tick_size = "0.005";
        params.neg_risk = false;

        server.enqueue(R"({"minimum_tick_size":"0.005"})");
        const auto off_grid = client.create_order_result(params);
        params.price = 0.515;
        const auto on_grid = client.create_order_result(params);
        params.price = std::nextafter(0.515, 0.0);
        const auto adjacent_representation = client.create_order_result(params);

        params.token_id = "1000";
        params.tick_size = "0.1";
        params.price = 0.45;
        server.enqueue(R"({"minimum_tick_size":"0.1"})");
        const auto rounded_onto_grid = client.create_order_result(params);

        params.price = 0.5;
        params.size = std::numeric_limits<double>::max();
        const auto oversized_limit = client.create_order_result(params);
        CreateMarketOrderParams market_params;
        market_params.token_id = params.token_id;
        market_params.amount = std::numeric_limits<double>::max();
        market_params.price = 0.5;
        market_params.tick_size = "0.1";
        market_params.neg_risk = false;
        const auto oversized_market =
            client.create_market_order_result(market_params, OrderType::FOK);
        const auto requests = server.requests();

        return check(!off_grid &&
                         off_grid.error().code == SdkErrorCode::InvalidArgument,
                     "0.513 must be rejected for a 0.005 tick") &&
               check(on_grid.ok(),
                     "0.515 must remain valid: " +
                         (on_grid ? std::string{} : on_grid.error().message)) &&
               check(adjacent_representation.ok(),
                     "the immediate 0.515 representation must remain valid: " +
                         (adjacent_representation ? std::string{} :
                                                    adjacent_representation.error().message)) &&
               check(on_grid.value().maker_amount == "1030000" &&
                         on_grid.value().taker_amount == "2000000",
                     "validated grid price must produce an exact signed ratio") &&
               check(!rounded_onto_grid &&
                         rounded_onto_grid.error().code == SdkErrorCode::InvalidArgument,
                     "public validation must reject 0.45 for a 0.1 tick before half-even rounding") &&
               check(!oversized_limit &&
                         oversized_limit.error().code == SdkErrorCode::InvalidArgument &&
                         !oversized_market &&
                         oversized_market.error().code == SdkErrorCode::InvalidArgument,
                     "finite amount overflow must be InvalidArgument, not Signing") &&
               check(requests.size() == 2,
                     "price-grid validation must reuse resolved tick metadata");
    }
}
