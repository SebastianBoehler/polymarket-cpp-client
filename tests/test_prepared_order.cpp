#include "clob_client.hpp"
#include "../src/clob_client_test_fixture.hpp"

#include <stdexcept>
#include <type_traits>

using namespace polymarket;

namespace
{
    template <typename Client>
    concept PostsRawOrderWithoutType = requires(Client &client,
                                                 const SignedOrder &order)
    {
        client.post_order(order);
        client.post_order_result(order);
    };

    template <typename Client>
    concept PostsPreparedOrder = requires(Client &client,
                                           const PreparedOrder &prepared)
    {
        client.post_order(prepared);
        client.post_order_result(prepared);
    };

    static_assert(!PostsRawOrderWithoutType<ClobClient>,
                  "raw signed orders must require an explicit OrderType");
    static_assert(PostsPreparedOrder<ClobClient>,
                  "prepared orders must post with their retained OrderType");
    static_assert(!std::is_constructible_v<PreparedOrder, SignedOrder>,
                  "preparing an order must require an explicit OrderType");
    static_assert(std::is_constructible_v<PreparedOrder, SignedOrder, OrderType>,
                  "prepared orders must accept a signed order and explicit type");

    SignedOrder sample_order()
    {
        SignedOrder order;
        order.salt = "1";
        return order;
    }

    CreateMarketOrderParams market_params()
    {
        CreateMarketOrderParams params;
        params.token_id = "123";
        params.amount = 1.0;
        params.price = 0.5;
        params.tick_size = "0.01";
        params.neg_risk = false;
        return params;
    }

    bool test_market_preparation_retains_type()
    {
        clob_test::LocalServer server;
        auto client = clob_test::authenticated_client(server.url());
        server.enqueue(R"({"minimum_tick_size":"0.01"})");
        const auto fak = client.create_market_order_result(
            market_params(), OrderType::FAK);
        const auto fok = client.create_market_order_result(
            market_params(), OrderType::FOK);

        const std::string preparation_error =
            (fak ? "" : " FAK: " + fak.error().message) +
            (fok ? "" : " FOK: " + fok.error().message);
        return clob_test::check(fak && fok,
                                "valid market orders must be prepared" +
                                    preparation_error) &&
               clob_test::check(fak.value().order_type == OrderType::FAK &&
                                    fok.value().order_type == OrderType::FOK,
                                "prepared market orders must retain FAK/FOK") &&
               clob_test::check(!fak.value().order.signature.empty() &&
                                    !fok.value().order.signature.empty(),
                                "prepared market orders must contain signed orders");
    }

    bool test_market_preparation_defaults_to_fak()
    {
        clob_test::LocalServer server;
        auto client = clob_test::authenticated_client(server.url());
        server.enqueue(R"({"minimum_tick_size":"0.01"})");
        const auto prepared = client.create_market_order(market_params());
        const auto result = client.create_market_order_result(market_params());

        return clob_test::check(prepared.order_type == OrderType::FAK,
                                "default market preparation must retain FAK") &&
               clob_test::check(result && result.value().order_type == OrderType::FAK,
                                "default market Result must retain FAK");
    }

    bool test_invalid_market_types_fail_before_network()
    {
        clob_test::LocalServer server;
        auto client = clob_test::authenticated_client(server.url());
        const auto gtc = client.create_market_order_result(
            market_params(), OrderType::GTC);
        const auto gtd = client.create_market_order_result(
            market_params(), OrderType::GTD);
        const auto unknown = client.create_market_order_result(
            market_params(), static_cast<OrderType>(99));
        const auto invalid = [](const auto &result)
        {
            return !result && result.error().code == SdkErrorCode::InvalidArgument;
        };

        return clob_test::check(invalid(gtc) && invalid(gtd) && invalid(unknown),
                                "invalid market types must be InvalidArgument") &&
               clob_test::check(server.requests().empty(),
                                "invalid market types must not resolve metadata");
    }

    bool test_prepared_post_uses_retained_type()
    {
        clob_test::LocalServer server;
        auto client = clob_test::authenticated_client(server.url());
        server.enqueue(R"({"success":true,"orderID":"order-1","status":"matched","makingAmount":"1","takingAmount":"2","transactionsHashes":[]})");
        const PreparedOrder prepared{sample_order(), OrderType::FAK};
        const auto posted = client.post_order_result(prepared);

        const auto invalid_type = static_cast<OrderType>(99);
        const auto invalid_raw = client.post_order_result(sample_order(), invalid_type);
        const auto invalid_prepared = client.post_order_result(
            PreparedOrder{sample_order(), invalid_type});
        const auto requests = server.requests();
        const auto body = requests.empty() ? nlohmann::json::object()
                                           : nlohmann::json::parse(requests[0].body);

        return clob_test::check(posted && posted.value().order_id == "order-1",
                                "prepared order posting must succeed") &&
               clob_test::check(body.value("orderType", "") == "FAK",
                                "prepared posting must use its retained type") &&
               clob_test::check(!invalid_raw && !invalid_prepared &&
                                    invalid_raw.error().code == SdkErrorCode::InvalidArgument &&
                                    invalid_prepared.error().code == SdkErrorCode::InvalidArgument,
                                "invalid post types must be InvalidArgument") &&
               clob_test::check(requests.size() == 1,
                                "invalid post types must fail before HTTP");
    }
}

int main()
{
    bool ok = test_market_preparation_retains_type();
    ok &= test_market_preparation_defaults_to_fak();
    ok &= test_invalid_market_types_fail_before_network();
    ok &= test_prepared_post_uses_retained_type();
    return ok ? 0 : 1;
}
