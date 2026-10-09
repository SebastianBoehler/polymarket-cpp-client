#include "../src/clob_client_test_fixture.hpp"
#include "check_support.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

using namespace polymarket;
using check_support::check;

namespace
{
    constexpr const char *accepted_order =
        R"({"success":true,"orderID":"order-1","status":"live"})";

    std::uint64_t unix_now_seconds()
    {
        const auto now = std::chrono::system_clock::now().time_since_epoch();
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::seconds>(now).count());
    }

    PlaceLimitOrderParams limit_order(double price)
    {
        PlaceLimitOrderParams params;
        params.token_id = "123";
        params.price = price;
        params.size = 10.0;
        params.side = OrderSide::BUY;
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

    void test_gtc_and_gtd_payloads()
    {
        clob_test::LocalServer server;
        auto client = clob_test::authenticated_client(server.url());
        server.enqueue(R"({"minimum_tick_size":"0.01"})");
        server.enqueue(R"({"neg_risk":false})");
        server.enqueue(accepted_order);
        const auto gtc = client.place_limit_order(limit_order(0.5));

        auto gtd_params = limit_order(0.5);
        gtd_params.post_only = true;
        gtd_params.expiration = unix_now_seconds() + 3600;
        server.enqueue(accepted_order);
        const auto gtd = client.place_limit_order(gtd_params);
        const auto requests = server.requests();

        check(gtc && gtd, "limit orders must be accepted");
        check(targets(requests) == std::vector<std::string>{"GET /tick-size?token_id=123",
                                                            "GET /neg-risk?token_id=123",
                                                            "POST /order", "POST /order"},
              "limit orders must resolve metadata once and post each order");
        if (requests.size() != 4) return;

        const auto gtc_body = nlohmann::json::parse(requests[2].body);
        check(gtc_body["orderType"] == "GTC" && gtc_body["postOnly"] == false &&
                  gtc_body["order"]["expiration"] == "0",
              "an order without expiration must post as GTC with expiration 0");
        check(gtc_body["order"]["makerAmount"] == "5000000" &&
                  gtc_body["order"]["takerAmount"] == "10000000",
              "limit BUY amounts mismatch");

        const auto gtd_body = nlohmann::json::parse(requests[3].body);
        check(gtd_body["orderType"] == "GTD" && gtd_body["postOnly"] == true &&
                  gtd_body["order"]["expiration"] == std::to_string(*gtd_params.expiration),
              "an order with expiration must post as post-only GTD with that expiration");
    }

    void test_off_grid_price_refreshes_tick_once()
    {
        clob_test::LocalServer server;
        auto client = clob_test::authenticated_client(server.url());
        server.enqueue(R"({"minimum_tick_size":"0.01"})");
        server.enqueue(R"({"neg_risk":false})");
        server.enqueue(accepted_order);
        (void)client.place_limit_order(limit_order(0.5));

        // The market moved to a 0.001 tick after the cached 0.01 was fetched.
        server.enqueue(R"({"minimum_tick_size":"0.001"})");
        server.enqueue(R"({"neg_risk":false})");
        server.enqueue(accepted_order);
        const auto refreshed = client.place_limit_order(limit_order(0.965));

        // Still off the refreshed grid: fail without posting.
        server.enqueue(R"({"minimum_tick_size":"0.001"})");
        const auto off_grid = client.place_limit_order(limit_order(0.9655));
        const auto requests = server.requests();

        check(refreshed.ok(), "a price on the refreshed grid must be accepted");
        check(!off_grid && off_grid.error().code == SdkErrorCode::InvalidArgument,
              "a price off the refreshed grid must be rejected");
        check(targets(requests) ==
                  std::vector<std::string>{
                      "GET /tick-size?token_id=123", "GET /neg-risk?token_id=123", "POST /order",
                      "GET /tick-size?token_id=123", "GET /neg-risk?token_id=123", "POST /order",
                      "GET /tick-size?token_id=123"},
              "off-grid prices must refetch metadata once and never post");
        if (requests.size() != 7) return;
        const auto body = nlohmann::json::parse(requests[5].body);
        check(body["order"]["makerAmount"] == "9650000",
              "refreshed order must be signed on the finer grid");
    }

    void test_rejects_before_any_request()
    {
        clob_test::LocalServer server;
        auto client = clob_test::authenticated_client(server.url());
        const auto invalid = [](const Result<OrderResponse> &result)
        { return !result && result.error().code == SdkErrorCode::InvalidArgument; };

        auto soon = limit_order(0.5);
        soon.expiration = unix_now_seconds() + 60;
        check(invalid(client.place_limit_order(soon)),
              "an expiration under 180 s ahead must be rejected");
        auto zero_expiration = limit_order(0.5);
        zero_expiration.expiration = 0;
        check(invalid(client.place_limit_order(zero_expiration)),
              "a GTD expiration of 0 must be rejected");
        check(invalid(client.place_limit_order(limit_order(1.0))), "price 1 must be rejected");
        auto no_size = limit_order(0.5);
        no_size.size = 0.0;
        check(invalid(client.place_limit_order(no_size)), "zero size must be rejected");
        auto no_token = limit_order(0.5);
        no_token.token_id.clear();
        check(invalid(client.place_limit_order(no_token)), "empty token_id must be rejected");

        ClobClient public_client(server.url(), 137);
        const auto unauthenticated = public_client.place_limit_order(limit_order(0.5));
        check(!unauthenticated && unauthenticated.error().code == SdkErrorCode::Auth,
              "a public client must not place orders");
        check(server.requests().empty(), "invalid orders must not reach the server");
    }

    void test_reports_server_rejection()
    {
        clob_test::LocalServer server;
        auto client = clob_test::authenticated_client(server.url());
        auto params = limit_order(0.5);
        params.neg_risk = false;
        server.enqueue(R"({"minimum_tick_size":"0.01"})");
        server.enqueue(
            R"({"success":false,"orderID":"","status":"","errorMsg":"not enough balance"})");
        const auto rejected = client.place_limit_order(params);
        server.enqueue(R"({"error":"no tick"})", 500);
        params.token_id = "456";
        const auto missing_tick = client.place_limit_order(params);

        check(!rejected && rejected.error().code == SdkErrorCode::ApiResponse &&
                  rejected.error().message == "not enough balance",
              "a rejected order must surface the server message");
        check(!missing_tick && missing_tick.error().code == SdkErrorCode::ApiResponse &&
                  missing_tick.error().http_status == 500 && missing_tick.error().retryable,
              "a tick size server error must keep its HTTP status");
        check(server.requests().size() == 3, "tick failure must stop before posting");
    }
} // namespace

int main()
{
    test_gtc_and_gtd_payloads();
    test_off_grid_price_refreshes_tick_once();
    test_rejects_before_any_request();
    test_reports_server_rejection();
    return check_support::finish("test_order_placement");
}
