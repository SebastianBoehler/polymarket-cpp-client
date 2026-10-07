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

    bool test_public_reads_retry_after_429()
    {
        LocalServer server;
        ClobClient client(server.url(), 137);
        const std::map<std::string, std::string> retry_now = {{"Retry-After", "0"}};

        server.enqueue(R"({"error":"slow down"})", 429, retry_now);
        server.enqueue(R"({"mid":"0.5"})");
        const auto midpoint = client.get_midpoint("token-1");
        server.enqueue(R"({"error":"slow down"})", 429, retry_now);
        server.enqueue(R"({"error":"slow down"})", 429, retry_now);
        server.enqueue(R"([])");
        const auto books = client.get_order_books({"token-1"});
        const auto requests = server.requests();

        return check(midpoint && requests.size() == 5, "GET reads must retry a 429") &&
               check(requests[0].target == requests[1].target && requests[2].method == "POST" &&
                         requests[2].target == "/books" && requests[4].target == "/books",
                     "batch POST reads must retry a 429 up to the default two times") &&
               check(books.empty(), "the retried batch read must return the final response");
    }

    // L2 timestamps have one-second resolution on the wall clock, so a 2 s
    // Retry-After leaves margin for clock adjustment between attempts.
    bool test_authenticated_reads_resign_each_attempt()
    {
        LocalServer server;
        auto client = authenticated_client(server.url());
        server.enqueue(R"({"error":"slow down"})", 429, {{"Retry-After", "2"}});
        server.enqueue(R"({"apiKeys":["test-key"]})");
        const auto keys = client.get_api_keys();
        const auto requests = server.requests();

        return check(keys == std::vector<std::string>{"test-key"} && requests.size() == 2,
                     "authenticated reads must retry a 429") &&
               check(requests[0].headers.at("poly_timestamp") !=
                         requests[1].headers.at("poly_timestamp"),
                     "each attempt must carry a fresh L2 timestamp") &&
               check(requests[1].headers.at("poly_signature") ==
                         expected_signature(requests[1], "/auth/api-keys"),
                     "the retried request must carry a valid signature");
    }

    bool test_data_api_reads_retry_after_429()
    {
        LocalServer server;
        auto environment = Environment::production();
        environment.clob_url = server.url();
        environment.data_url = server.url();
        ClobClient client(environment);
        server.enqueue(R"({"error":"slow down"})", 429, {{"Retry-After", "0"}});
        server.enqueue(R"([])");
        const auto positions = client.get_positions("0x1111111111111111111111111111111111111111");
        const auto requests = server.requests();

        return check(positions.empty() && requests.size() == 2 &&
                         requests[1].target.rfind("/positions?", 0) == 0,
                     "Data API reads must retry a 429");
    }

    bool test_retry_policy_limits()
    {
        LocalServer server;
        ClobClient client(server.url(), 137);
        server.enqueue(R"({"error":"slow down"})", 429, {{"Retry-After", "10"}});
        const bool long_wait_failed = !client.get_midpoint("token-1");
        const auto after_long_wait = server.requests().size();

        client.set_rate_limit_retry(std::nullopt);
        server.enqueue(R"({"error":"slow down"})", 429, {{"Retry-After", "0"}});
        const bool disabled_failed = !client.get_midpoint("token-1");
        const auto after_disabled = server.requests().size();

        client.set_rate_limit_retry(RateLimitRetry{1, std::chrono::milliseconds(5000)});
        server.enqueue(R"({"error":"slow down"})", 429, {{"Retry-After", "0"}});
        server.enqueue(R"({"error":"slow down"})", 429, {{"Retry-After", "0"}});
        const bool exhausted_failed = !client.get_midpoint("token-1");
        const auto after_exhausted = server.requests().size();

        bool rejected_invalid = false;
        try
        {
            client.set_rate_limit_retry(RateLimitRetry{-1, std::chrono::milliseconds(0)});
        }
        catch (const std::invalid_argument &)
        {
            rejected_invalid = true;
        }

        return check(long_wait_failed && after_long_wait == 1,
                     "a Retry-After above max_delay must fail without retrying") &&
               check(disabled_failed && after_disabled == 2, "a disabled policy must not retry") &&
               check(exhausted_failed && after_exhausted == 4,
                     "retries must stop at the policy count") &&
               check(rejected_invalid, "an invalid policy must be rejected");
    }

    bool test_writes_are_not_retried()
    {
        LocalServer server;
        auto client = authenticated_client(server.url());
        server.enqueue(R"({"error":"slow down"})", 429, {{"Retry-After", "0"}});
        const bool cancelled = client.cancel_all();
        server.enqueue(R"({"error":"slow down"})", 429, {{"Retry-After", "0"}});
        const bool deleted = client.delete_api_key();

        return check(!cancelled && !deleted && server.requests().size() == 2,
                     "cancellations and other writes must not be retried");
    }
} // namespace clob_test

int main()
{
    using namespace clob_test;
    polymarket::http_global_init();
    const bool ok = test_listener_receives_order_and_cancel_buckets() &&
                    test_rate_limited_order_is_not_retried() &&
                    test_throwing_listener_and_removal() && test_public_reads_retry_after_429() &&
                    test_authenticated_reads_resign_each_attempt() &&
                    test_data_api_reads_retry_after_429() && test_retry_policy_limits() &&
                    test_writes_are_not_retried();
    polymarket::http_global_cleanup();
    return ok ? 0 : 1;
}
