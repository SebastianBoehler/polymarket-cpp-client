#include "clob_client.hpp"
#include "sdk_error.hpp"

#include <iostream>
#include <string>

using namespace polymarket;

namespace
{
    bool expect_true(const std::string &name, bool value)
    {
        if (value)
        {
            return true;
        }
        std::cerr << "failed: " << name << "\n";
        return false;
    }

    bool expect_equal(const std::string &name, const std::string &actual, const std::string &expected)
    {
        if (actual == expected)
        {
            return true;
        }
        std::cerr << name << " mismatch\n"
                  << "  expected: " << expected << "\n"
                  << "  actual:   " << actual << "\n";
        return false;
    }

    bool expect_code(const std::string &name, SdkErrorCode actual, SdkErrorCode expected)
    {
        return expect_true(name, actual == expected);
    }
}

int main()
{
    HttpResponse auth_response;
    auth_response.status_code = 401;
    auth_response.body = R"({"error":"bad signature"})";
    auth_response.headers["x-request-id"] = "req-1";
    const auto auth_error = make_sdk_error(auth_response, "/orders");
    if (!expect_code("auth classification", auth_error.code, SdkErrorCode::Auth) ||
        !expect_equal("auth message", auth_error.message, "bad signature") ||
        !expect_equal("request id", auth_error.request_id, "req-1") ||
        !expect_true("auth not retryable", !auth_error.retryable))
    {
        return 1;
    }

    HttpResponse rate_limit_response;
    rate_limit_response.status_code = 429;
    rate_limit_response.body = R"({"message":"too many requests"})";
    const auto rate_limit_error = make_sdk_error(rate_limit_response, "/order");
    if (!expect_code("rate limit classification", rate_limit_error.code, SdkErrorCode::RateLimit) ||
        !expect_true("rate limit retryable", rate_limit_error.retryable))
    {
        return 1;
    }

    HttpResponse api_response;
    api_response.status_code = 500;
    api_response.body = R"({"detail":"upstream failed"})";
    const auto api_error = make_sdk_error(api_response, "/book");
    if (!expect_code("non-2xx classification", api_error.code, SdkErrorCode::ApiResponse) ||
        !expect_true("server retryable", api_error.retryable) ||
        !expect_equal("api message", api_error.message, "upstream failed"))
    {
        return 1;
    }

    HttpResponse curl_response;
    curl_response.error = "Operation timed out";
    const auto curl_error = make_sdk_error(curl_response, "/markets");
    if (!expect_code("curl classification", curl_error.code, SdkErrorCode::HttpTransport) ||
        !expect_true("curl retryable", curl_error.retryable) ||
        !expect_equal("curl message", curl_error.message, "Operation timed out"))
    {
        return 1;
    }

    const auto parse_error = make_parse_error("invalid json", "/orders", "{not-json");
    if (!expect_code("parse classification", parse_error.code, SdkErrorCode::Parse) ||
        !expect_equal("parse excerpt", parse_error.response_body_excerpt, "{not-json"))
    {
        return 1;
    }

    ClobClient client;
    CreateOrderParams params;
    params.token_id = "123";
    params.price = 0.5;
    params.size = 1.0;
    params.side = OrderSide::BUY;
    const auto unauthenticated = client.create_order_result(params);
    if (!expect_true("unauthenticated result fails", !unauthenticated) ||
        !expect_code("unauthenticated code", unauthenticated.error().code, SdkErrorCode::Auth))
    {
        return 1;
    }

    return 0;
}
