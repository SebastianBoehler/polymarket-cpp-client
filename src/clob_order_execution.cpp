#include "polymarket/clob_client.hpp"
#include "order_execution.hpp"
#include "polymarket/order_signer.hpp"
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace polymarket
{
    namespace
    {
        constexpr const char *ZERO_ADDRESS = "0x0000000000000000000000000000000000000000";

        struct MetadataResolutionError : std::runtime_error
        {
            MetadataResolutionError(const std::string &context, SdkError cause)
                : std::runtime_error(context + ": " + cause.message), error(std::move(cause))
            {
            }

            SdkError error;
        };

        constexpr bool is_market_order_type(OrderType order_type)
        {
            return order_type == OrderType::FAK || order_type == OrderType::FOK;
        }

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

        std::uint64_t price_scale(const detail::OrderRoundingConfig &rounding)
        {
            std::uint64_t scale = 1;
            for (int index = 0; index < rounding.price_decimals; ++index)
                scale *= 10;
            return scale;
        }

        std::string resolve_market_tick_size(const Result<TickSizeInfo> &tick_info)
        {
            if (!tick_info)
            {
                throw MetadataResolutionError("could not resolve market tick size",
                                              tick_info.error());
            }
            const auto &market_tick_size = tick_info.value().minimum_tick_size;
            (void)detail::rounding_config_for_tick_size(market_tick_size);
            return market_tick_size;
        }

        std::string resolve_requested_tick_size(const std::string &requested_tick_size,
                                                const std::optional<TickSizeInfo> &market_tick)
        {
            const auto requested = detail::rounding_config_for_tick_size(
                requested_tick_size);
            if (!market_tick)
            {
                return requested_tick_size;
            }
            const auto minimum =
                detail::rounding_config_for_tick_size(market_tick->minimum_tick_size);
            const auto minimum_scale = price_scale(minimum);
            const auto requested_scale = price_scale(requested);
            if (requested.tick_units * minimum_scale <
                minimum.tick_units * requested_scale)
            {
                throw std::invalid_argument("requested tick size is smaller than the market minimum");
            }
            return requested_tick_size;
        }

        bool resolve_neg_risk(const Result<NegRiskInfo> &neg_risk_info)
        {
            if (!neg_risk_info)
            {
                throw MetadataResolutionError("could not resolve neg-risk metadata",
                                              neg_risk_info.error());
            }
            return neg_risk_info.value().neg_risk;
        }
    }

    SignedOrder ClobClient::create_order(const CreateOrderParams &params)
    {
        if (!order_signer_)
        {
            throw std::runtime_error("Client not authenticated");
        }

        const std::string tick_size =
            params.tick_size.empty()
                ? resolve_market_tick_size(tick_size_result(params.token_id))
                : resolve_requested_tick_size(params.tick_size, cached_tick_size(params.token_id));
        const auto validated_price = detail::validate_order_price(
            params.price, tick_size);
        const bool is_neg_risk =
            params.neg_risk ? *params.neg_risk : resolve_neg_risk(neg_risk_result(params.token_id));

        const auto context = build_execution_context(*this, *order_signer_, funder_address_, sig_type_);
        const auto amounts = detail::calculate_limit_order_amounts(
            params.side,
            params.size,
            validated_price);

        OrderData order_data;
        order_data.maker = context.maker_address();
        order_data.taker = ZERO_ADDRESS;
        order_data.token_id = params.token_id;
        order_data.maker_amount = std::to_string(amounts.maker);
        order_data.taker_amount = std::to_string(amounts.taker);
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
        catch (const std::invalid_argument &ex)
        {
            return Result<SignedOrder>::failure({SdkErrorCode::InvalidArgument, ex.what(), "/order", 0, "", "", false});
        }
        catch (const std::out_of_range &ex)
        {
            return Result<SignedOrder>::failure({SdkErrorCode::InvalidArgument, ex.what(), "/order", 0, "", "", false});
        }
        catch (const MetadataResolutionError &ex)
        {
            return Result<SignedOrder>::failure(ex.error);
        }
        catch (const std::exception &ex)
        {
            return Result<SignedOrder>::failure({SdkErrorCode::Signing, ex.what(), "/order", 0, "", "", false});
        }
    }

    PreparedOrder ClobClient::create_market_order(const CreateMarketOrderParams &params)
    {
        return create_market_order(params, OrderType::FAK);
    }

    PreparedOrder ClobClient::create_market_order(const CreateMarketOrderParams &params,
                                                  OrderType order_type)
    {
        if (!is_market_order_type(order_type))
            throw std::invalid_argument("Market orders require FAK or FOK");
        if (!order_signer_)
        {
            throw std::runtime_error("Client not authenticated");
        }

        std::optional<Orderbook> book;
        if (!params.price)
        {
            auto fetched = order_book_result(params.token_id);
            if (!fetched)
            {
                throw MetadataResolutionError("could not resolve executable market price",
                                              fetched.error());
            }
            book = std::move(fetched.value());
        }
        const std::string tick_size =
            params.tick_size.empty()
                ? resolve_market_tick_size(tick_size_result(params.token_id))
                : resolve_requested_tick_size(params.tick_size, cached_tick_size(params.token_id));
        const double price = params.price
                                 ? *params.price
                                 : detail::calculate_market_price(*book, params.side, params.amount,
                                                                  order_type, tick_size);
        const auto validated_price = detail::validate_order_price(price, tick_size);

        const bool is_neg_risk =
            params.neg_risk ? *params.neg_risk : resolve_neg_risk(neg_risk_result(params.token_id));

        const auto context = build_execution_context(*this, *order_signer_, funder_address_, sig_type_);
        const auto amounts = detail::calculate_market_order_amounts(
            params.side,
            params.amount,
            validated_price);

        OrderData order_data;
        order_data.maker = context.maker_address();
        order_data.taker = ZERO_ADDRESS;
        order_data.token_id = params.token_id;
        order_data.maker_amount = std::to_string(amounts.maker);
        order_data.taker_amount = std::to_string(amounts.taker);
        order_data.side = params.side;
        order_data.signer = context.signer_for_order();
        order_data.metadata = params.metadata;
        order_data.builder = params.builder_code;
        order_data.signature_type = sig_type_;

        return {order_signer_->sign_order(order_data, context.exchange_for(is_neg_risk)),
                order_type};
    }

    Result<PreparedOrder> ClobClient::create_market_order_result(
        const CreateMarketOrderParams &params)
    {
        return create_market_order_result(params, OrderType::FAK);
    }

    Result<PreparedOrder> ClobClient::create_market_order_result(
        const CreateMarketOrderParams &params,
        OrderType order_type)
    {
        if (!is_market_order_type(order_type))
        {
            return Result<PreparedOrder>::failure({SdkErrorCode::InvalidArgument,
                                                   "Market orders require FAK or FOK",
                                                   "/order", 0, "", "", false});
        }
        if (!order_signer_)
        {
            return Result<PreparedOrder>::failure(
                make_auth_error("Client not authenticated", "/order"));
        }
        if (params.amount <= 0.0)
        {
            return Result<PreparedOrder>::failure({SdkErrorCode::InvalidArgument,
                                                   "Market order amount must be positive",
                                                   "/order", 0, "", "", false});
        }

        try
        {
            return Result<PreparedOrder>::success(
                create_market_order(params, order_type));
        }
        catch (const std::invalid_argument &ex)
        {
            return Result<PreparedOrder>::failure({SdkErrorCode::InvalidArgument, ex.what(), "/order", 0, "", "", false});
        }
        catch (const std::out_of_range &ex)
        {
            return Result<PreparedOrder>::failure({SdkErrorCode::InvalidArgument, ex.what(), "/order", 0, "", "", false});
        }
        catch (const MetadataResolutionError &ex)
        {
            return Result<PreparedOrder>::failure(ex.error);
        }
        catch (const std::exception &ex)
        {
            return Result<PreparedOrder>::failure({SdkErrorCode::Signing, ex.what(), "/order", 0, "", "", false});
        }
    }
}
