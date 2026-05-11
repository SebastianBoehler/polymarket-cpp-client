#include "clob_client.hpp"
#include "order_execution.hpp"
#include "order_signer.hpp"
#include <nlohmann/json.hpp>
#include <stdexcept>

using json = nlohmann::json;

namespace polymarket
{
    namespace
    {
        constexpr const char *ZERO_ADDRESS = "0x0000000000000000000000000000000000000000";

        detail::OrderExecutionContext build_execution_context(const ClobClient &client,
                                                              const OrderSigner &signer,
                                                              const std::string &funder,
                                                              SignatureType signature_type)
        {
            return {
                signer.address(),
                funder,
                signature_type,
                client.get_exchange_address(),
                client.get_neg_risk_exchange_address()};
        }

        SdkError order_response_error(const OrderResponse &response, const std::string &endpoint)
        {
            SdkError error;
            error.code = SdkErrorCode::ApiResponse;
            error.message = response.error_msg.empty() ? "Order request failed" : response.error_msg;
            error.endpoint = endpoint;
            error.retryable = false;
            return error;
        }
    }

    SignedOrder ClobClient::create_order(const CreateOrderParams &params)
    {
        if (!order_signer_)
        {
            throw std::runtime_error("Client not authenticated");
        }

        bool is_neg_risk = false;
        if (params.neg_risk.has_value())
        {
            is_neg_risk = params.neg_risk.value();
        }
        else
        {
            auto neg_risk_info = get_neg_risk(params.token_id);
            is_neg_risk = neg_risk_info && neg_risk_info->neg_risk;
        }

        const auto context = build_execution_context(*this, *order_signer_, funder_address_, sig_type_);
        const auto amounts = detail::calculate_limit_order_amounts(params.side, params.price, params.size);

        OrderData order_data;
        order_data.maker = context.maker_address();
        order_data.taker = ZERO_ADDRESS;
        order_data.token_id = params.token_id;
        order_data.maker_amount = to_wei(amounts.maker, 6);
        order_data.taker_amount = to_wei(amounts.taker, 6);
        order_data.side = params.side;
        order_data.signer = context.signer_for_order();
        order_data.expiration = params.expiration;
        order_data.metadata = params.metadata;
        order_data.builder = params.builder_code;
        order_data.signature_type = sig_type_;

        return order_signer_->sign_order(order_data, context.exchange_for(is_neg_risk));
    }

    Result<SignedOrder> ClobClient::create_order_result(const CreateOrderParams &params)
    {
        if (!order_signer_)
        {
            return Result<SignedOrder>::failure(make_auth_error("Client not authenticated", "/order"));
        }
        if (params.price <= 0.0 || params.size <= 0.0)
        {
            return Result<SignedOrder>::failure({SdkErrorCode::InvalidArgument,
                                                "Order price and size must be positive",
                                                "/order",
                                                0,
                                                "",
                                                "",
                                                false});
        }

        try
        {
            return Result<SignedOrder>::success(create_order(params));
        }
        catch (const std::exception &ex)
        {
            return Result<SignedOrder>::failure({SdkErrorCode::Signing, ex.what(), "/order", 0, "", "", false});
        }
    }

    SignedOrder ClobClient::create_market_order(const CreateMarketOrderParams &params)
    {
        if (!order_signer_)
        {
            throw std::runtime_error("Client not authenticated");
        }

        double price = 0.5;
        if (params.price)
        {
            price = *params.price;
        }
        else
        {
            auto price_info = get_price(params.token_id, params.side == OrderSide::BUY ? "buy" : "sell");
            if (price_info)
            {
                price = price_info->price;
            }
        }

        CreateOrderParams order_params;
        order_params.token_id = params.token_id;
        order_params.price = price;
        order_params.size = params.side == OrderSide::BUY ? params.amount / price : params.amount;
        order_params.side = params.side;
        order_params.metadata = params.metadata;
        order_params.builder_code = params.builder_code;

        return create_order(order_params);
    }

    Result<SignedOrder> ClobClient::create_market_order_result(const CreateMarketOrderParams &params)
    {
        if (!order_signer_)
        {
            return Result<SignedOrder>::failure(make_auth_error("Client not authenticated", "/order"));
        }
        if (params.amount <= 0.0)
        {
            return Result<SignedOrder>::failure({SdkErrorCode::InvalidArgument,
                                                "Market order amount must be positive",
                                                "/order",
                                                0,
                                                "",
                                                "",
                                                false});
        }

        try
        {
            return Result<SignedOrder>::success(create_market_order(params));
        }
        catch (const std::exception &ex)
        {
            return Result<SignedOrder>::failure({SdkErrorCode::Signing, ex.what(), "/order", 0, "", "", false});
        }
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
        if (!order_signer_ || !api_creds_)
        {
            return Result<OrderResponse>::failure(make_auth_error("Client not authenticated", "/order"));
        }

        const auto body = detail::order_payload_json(order,
                                                     api_creds_ ? api_creds_->api_key : "",
                                                     order_type_to_string(order_type));
        const std::string body_str = body.dump();
        auto headers = get_l2_headers("POST", "/order", body_str);
        auto response = http_.post("/order", body_str, headers);

        if (!response.ok())
        {
            return Result<OrderResponse>::failure(make_sdk_error(response, "/order"));
        }

        try
        {
            auto parsed_json = json::parse(response.body);
            (void)parsed_json;
        }
        catch (const std::exception &ex)
        {
            return Result<OrderResponse>::failure(make_parse_error(ex.what(), "/order", response.body));
        }

        auto parsed = parse_order_response(response.body);
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

        try
        {
            auto response_json = json::parse(response.body);
            if (response_json.is_array())
            {
                for (const auto &item : response_json)
                {
                    results.push_back(parse_order_response(item.dump()));
                }
            }
        }
        catch (...)
        {
            results.push_back(parse_order_response(response.body));
        }

        return results;
    }

    OrderResponse ClobClient::create_and_post_order(const CreateOrderParams &params, OrderType order_type)
    {
        return post_order(create_order(params), order_type);
    }

    OrderResponse ClobClient::create_and_post_market_order(const CreateMarketOrderParams &params, OrderType order_type)
    {
        return post_order(create_market_order(params), order_type);
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

        auto response = http_.post("/order", body_str, headers);
        if (!response.ok())
        {
            return Result<bool>::failure(make_sdk_error(response, "/order"));
        }
        return Result<bool>::success(true);
    }

    bool ClobClient::cancel_orders(const std::vector<std::string> &order_ids)
    {
        const json body = order_ids;

        const std::string body_str = body.dump();
        auto headers = get_l2_headers("DELETE", "/orders", body_str);

        auto response = http_.post("/orders", body_str, headers);
        return response.ok();
    }

    bool ClobClient::cancel_all()
    {
        auto headers = get_l2_headers("DELETE", "/cancel-all", "");
        auto response = http_.post("/cancel-all", "{}", headers);
        return response.ok();
    }

    bool ClobClient::cancel_market_orders(const std::string &condition_id)
    {
        json body;
        body["market"] = condition_id;

        const std::string body_str = body.dump();
        auto headers = get_l2_headers("DELETE", "/cancel-market-orders", body_str);

        auto response = http_.post("/cancel-market-orders", body_str, headers);
        return response.ok();
    }
}
