#include "clob_client.hpp"
#include "clob_client_internal.hpp"
#include "order_execution.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <stdexcept>

using json = nlohmann::json;

namespace polymarket
{
    namespace
    {
        SdkError order_response_error(const OrderResponse &response, const std::string &endpoint)
        {
            SdkError error;
            error.code = SdkErrorCode::ApiResponse;
            error.message = response.error_msg.empty() ? "Order request failed" : response.error_msg;
            error.endpoint = endpoint;
            error.retryable = false;
            return error;
        }

        SdkError cancellation_response_error(const std::string &body,
                                             const std::string &message,
                                             const std::string &endpoint)
        {
            constexpr std::size_t max_excerpt = 512;
            SdkError error;
            error.code = SdkErrorCode::ApiResponse;
            error.message = message;
            error.endpoint = endpoint;
            error.response_body_excerpt = body.substr(0, max_excerpt);
            return error;
        }

        OrderResponse failed_order_response(const std::string &message)
        {
            OrderResponse response;
            response.error_msg = message;
            return response;
        }

        std::vector<OrderResponse> failed_order_responses(std::size_t count,
                                                          const std::string &message)
        {
            return std::vector<OrderResponse>(count, failed_order_response(message));
        }

        bool confirms_cancellations(const detail::ParsedCancellationResponse &response,
                                    const std::vector<std::string> &expected)
        {
            if (!response.not_canceled.empty()) return false;
            return std::all_of(expected.begin(), expected.end(), [&](const auto &id)
                               { return response.canceled.contains(id); });
        }

        bool confirms_bulk_cancellation(const HttpResponse &response)
        {
            if (!response.ok()) return false;
            try
            {
                return detail::parse_cancellation_response_json(response.body)
                    .not_canceled.empty();
            }
            catch (...)
            {
                return false;
            }
        }
    }

    OrderResponse ClobClient::post_order(const PreparedOrder &prepared)
    {
        return post_order(prepared.order, prepared.order_type);
    }

    Result<OrderResponse> ClobClient::post_order_result(const PreparedOrder &prepared)
    {
        return post_order_result(prepared.order, prepared.order_type);
    }

    OrderResponse ClobClient::post_order(const SignedOrder &order, OrderType order_type)
    {
        auto result = post_order_result(order, order_type);
        if (result)
        {
            return result.value();
        }

        OrderResponse response;
        response.success = false;
        response.error_msg = result.error().message;
        return response;
    }

    Result<OrderResponse> ClobClient::post_order_result(const SignedOrder &order, OrderType order_type)
    {
        std::string serialized_type;
        try
        {
            serialized_type = order_type_to_string(order_type);
        }
        catch (const std::invalid_argument &error)
        {
            return Result<OrderResponse>::failure({SdkErrorCode::InvalidArgument,
                                                   error.what(), "/order",
                                                   0, "", "", false});
        }
        if (!order_signer_ || !api_creds_)
        {
            return Result<OrderResponse>::failure(make_auth_error("Client not authenticated", "/order"));
        }

        const auto body = detail::order_payload_json(order,
                                                     api_creds_ ? api_creds_->api_key : "",
                                                     serialized_type);
        const std::string body_str = body.dump();
        auto headers = get_l2_headers("POST", "/order", body_str);
        auto response = http_.post("/order", body_str, headers);

        if (!response.ok())
        {
            return Result<OrderResponse>::failure(make_sdk_error(response, "/order"));
        }

        OrderResponse parsed;
        try
        {
            parsed = detail::parse_order_response_json_strict(response.body);
        }
        catch (const std::exception &ex)
        {
            return Result<OrderResponse>::failure(make_parse_error(ex.what(), "/order", response.body));
        }

        if (!parsed.success)
        {
            return Result<OrderResponse>::failure(order_response_error(parsed, "/order"));
        }

        return Result<OrderResponse>::success(parsed);
    }

    std::vector<OrderResponse> ClobClient::post_orders(const std::vector<BatchOrderEntry> &orders)
    {
        std::vector<OrderResponse> results;
        if (orders.empty())
        {
            return results;
        }

        const auto body = detail::batch_order_payload_json(
            orders,
            api_creds_ ? api_creds_->api_key : "",
            [this](OrderType type)
            {
                return order_type_to_string(type);
            });

        const std::string body_str = body.dump();
        auto headers = get_l2_headers("POST", "/orders", body_str);
        auto response = http_.post("/orders", body_str, headers);

        if (!response.ok())
        {
            const auto error = make_sdk_error(response, "/orders");
            return failed_order_responses(orders.size(), error.message);
        }

        try
        {
            auto response_json = json::parse(response.body);
            if (!response_json.is_array())
            {
                return failed_order_responses(
                    orders.size(), "Batch order response must be an array");
            }
            if (response_json.size() != orders.size())
            {
                return failed_order_responses(
                    orders.size(), "Batch order response count did not match request");
            }
            for (const auto &item : response_json)
            {
                try
                {
                    results.push_back(
                        detail::parse_order_response_json_strict(item.dump()));
                }
                catch (const std::exception &ex)
                {
                    results.push_back(failed_order_response(
                        std::string("Invalid batch order response: ") + ex.what()));
                }
            }
        }
        catch (const std::exception &ex)
        {
            return failed_order_responses(
                orders.size(), std::string("Invalid batch order response: ") + ex.what());
        }

        return results;
    }

    OrderResponse ClobClient::create_and_post_order(const CreateOrderParams &params, OrderType order_type)
    {
        return post_order(create_order(params), order_type);
    }

    OrderResponse ClobClient::create_and_post_market_order(const CreateMarketOrderParams &params, OrderType order_type)
    {
        return post_order(create_market_order(params, order_type));
    }

    bool ClobClient::cancel_order(const std::string &order_id)
    {
        auto result = cancel_order_result(order_id);
        return result && result.value();
    }

    Result<bool> ClobClient::cancel_order_result(const std::string &order_id)
    {
        if (!order_signer_ || !api_creds_)
        {
            return Result<bool>::failure(make_auth_error("Client not authenticated", "/order"));
        }

        json body;
        body["orderID"] = order_id;

        const std::string body_str = body.dump();
        auto headers = get_l2_headers("DELETE", "/order", body_str);

        auto response = http_.del("/order", body_str, headers);
        if (!response.ok())
        {
            return Result<bool>::failure(make_sdk_error(response, "/order"));
        }
        try
        {
            const auto parsed = detail::parse_cancellation_response_json(response.body);
            if (!confirms_cancellations(parsed, {order_id}))
            {
                return Result<bool>::failure(cancellation_response_error(
                    response.body, "Order cancellation was not confirmed", "/order"));
            }
            return Result<bool>::success(true);
        }
        catch (const std::exception &ex)
        {
            return Result<bool>::failure(
                make_parse_error(ex.what(), "/order", response.body));
        }
    }

    bool ClobClient::cancel_orders(const std::vector<std::string> &order_ids)
    {
        const json body = order_ids;

        const std::string body_str = body.dump();
        auto headers = get_l2_headers("DELETE", "/orders", body_str);

        auto response = http_.del("/orders", body_str, headers);
        if (!response.ok()) return false;
        try
        {
            return confirms_cancellations(
                detail::parse_cancellation_response_json(response.body), order_ids);
        }
        catch (...)
        {
            return false;
        }
    }

    bool ClobClient::cancel_all()
    {
        auto headers = get_l2_headers("DELETE", "/cancel-all", "");
        auto response = http_.del("/cancel-all", "", headers);
        return confirms_bulk_cancellation(response);
    }

    bool ClobClient::cancel_market_orders(const std::string &condition_id)
    {
        json body;
        body["market"] = condition_id;

        const std::string body_str = body.dump();
        auto headers = get_l2_headers("DELETE", "/cancel-market-orders", body_str);

        auto response = http_.del("/cancel-market-orders", body_str, headers);
        return confirms_bulk_cancellation(response);
    }
}
