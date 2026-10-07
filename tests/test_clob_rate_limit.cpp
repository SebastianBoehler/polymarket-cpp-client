#include "../src/clob_client_test_fixture.hpp"

#include <vector>

namespace clob_test
{
    namespace
    {
        const std::map<std::string, std::string> rate_limit_headers = {
            {"Poly-RateLimit-Remaining", "12"},
            {"Poly-RateLimit-Reset", "1700000000"},
            {"Poly-RateLimit-Tier", "standard"},
            {"Poly-RateLimit-Warning", "true"},
        };

        constexpr const char *accepted_order =
            R"({"success":true,"orderID":"order-1","status":"live"})";
        constexpr const char *confirmed_cancel = R"({"canceled":["order-1"],"not_canceled":{}})";

        SignedOrder test_order()
        {
            SignedOrder order;
            order.salt = "1";
            return order;
        }
    } // namespace

    bool test_listener_receives_order_and_cancel_buckets()
    {
        LocalServer server;
        auto client = authenticated_client(server.url());
        std::vector<RateLimitUpdate> updates;
        client.set_rate_limit_listener([&updates](const RateLimitUpdate &update)
                                       { updates.push_back(update); });

        server.enqueue(accepted_order, 200, rate_limit_headers);
        const auto posted = client.post_order_result(test_order(), OrderType::GTC);
        server.enqueue(confirmed_cancel, 200, {{"Poly-RateLimit-Remaining", "-3"}});
        const auto cancelled = client.cancel_order_result("order-1");
        server.enqueue(accepted_order);
        (void)client.post_order_result(test_order(), OrderType::GTC);

        return check(posted && cancelled, "order and cancel requests must still succeed") &&
               check(updates.size() == 2,
                     "only responses with rate-limit headers notify the listener") &&
               check(updates[0].bucket == RateLimitUpdate::Bucket::Order &&
                         updates[0].remaining == 12 && updates[0].reset == 1700000000 &&
                         updates[0].tier == "standard" && updates[0].warning,
                     "order responses must report the order bucket and every header") &&
               check(updates[1].bucket == RateLimitUpdate::Bucket::Cancel &&
                         updates[1].remaining == -3 && !updates[1].warning,
                     "cancel responses must report the cancel bucket");
    }

    bool test_rate_limited_order_is_not_retried()
    {
        LocalServer server;
        auto client = authenticated_client(server.url());
        int notifications = 0;
        client.set_rate_limit_listener([&notifications](const RateLimitUpdate &)
                                       { ++notifications; });

        auto headers = rate_limit_headers;
        headers["Retry-After"] = "0";
        server.enqueue(R"({"error":"too many requests"})", 429, headers);
        const auto result = client.post_order_result(test_order(), OrderType::GTC);

        return check(!result && result.error().code == SdkErrorCode::RateLimit &&
                         result.error().retry_after_seconds == 0.0 && result.error().rate_limit &&
                         result.error().rate_limit->remaining == 12,
                     "a rate-limited order must return RateLimit with Retry-After and state") &&
               check(server.requests().size() == 1, "orders must never be retried automatically") &&
               check(notifications == 1, "a rate-limited response must still notify the listener");
    }

    bool test_throwing_listener_and_removal()
    {
        LocalServer server;
        auto client = authenticated_client(server.url());
        int calls = 0;
        client.set_rate_limit_listener(
            [&calls](const RateLimitUpdate &)
            {
                ++calls;
                throw std::runtime_error("listener failure");
            });

        server.enqueue(accepted_order, 200, rate_limit_headers);
        const auto posted = client.post_order_result(test_order(), OrderType::GTC);
        client.set_rate_limit_listener({});
        server.enqueue(confirmed_cancel, 200, rate_limit_headers);
        (void)client.cancel_all();

        return check(posted && calls == 1,
                     "a throwing listener must not change the request result") &&
               check(calls == 1, "an empty listener must stop notifications");
    }
} // namespace clob_test

int main()
{
    using namespace clob_test;
    polymarket::http_global_init();
    const bool ok = test_listener_receives_order_and_cancel_buckets() &&
                    test_rate_limited_order_is_not_retried() &&
                    test_throwing_listener_and_removal();
    polymarket::http_global_cleanup();
    return ok ? 0 : 1;
}
