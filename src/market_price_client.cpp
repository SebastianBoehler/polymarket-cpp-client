#include "order_execution.hpp"
#include "polymarket/clob_client.hpp"
#include "polymarket/market_price.hpp"

#include <cmath>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace polymarket
{
    namespace
    {
        SdkError estimate_error(SdkErrorCode code, const std::string &message,
                                const std::string &endpoint = "", bool retryable = false)
        {
            return {code, message, endpoint, 0, "", "", retryable};
        }

        std::optional<SdkError> validate_estimate_request(OrderSide side, double amount,
                                                          OrderType order_type)
        {
            if (side != OrderSide::BUY && side != OrderSide::SELL)
                return estimate_error(SdkErrorCode::InvalidArgument,
                                      "order side must be BUY or SELL");
            if (order_type != OrderType::FAK && order_type != OrderType::FOK)
                return estimate_error(SdkErrorCode::InvalidArgument,
                                      "Market orders require FAK or FOK");
            if (!std::isfinite(amount) || amount <= 0.0)
                return estimate_error(SdkErrorCode::InvalidArgument,
                                      "market order amount must be finite and positive");
            return std::nullopt;
        }
    } // namespace

    Result<MarketPriceEstimate> estimate_market_price(const Orderbook &book, OrderSide side,
                                                      double amount, const std::string &tick_size,
                                                      OrderType order_type)
    {
        if (auto error = validate_estimate_request(side, amount, order_type))
            return Result<MarketPriceEstimate>::failure(std::move(*error));
        if ((side == OrderSide::BUY ? book.asks : book.bids).empty())
            return Result<MarketPriceEstimate>::failure(
                estimate_error(SdkErrorCode::InsufficientLiquidity, "no matching orders"));

        try
        {
            auto estimate = detail::estimate_market_depth(book, side, amount, tick_size);
            if (!estimate.fully_fillable && order_type == OrderType::FOK)
            {
                return Result<MarketPriceEstimate>::failure(
                    estimate_error(SdkErrorCode::InsufficientLiquidity,
                                   "insufficient orderbook depth for FOK order"));
            }
            return Result<MarketPriceEstimate>::success(estimate);
        }
        catch (const std::logic_error &ex)
        {
            return Result<MarketPriceEstimate>::failure(
                estimate_error(SdkErrorCode::InvalidArgument, ex.what()));
        }
    }

    Result<MarketPriceEstimate> ClobClient::estimate_market_price(const std::string &token_id,
                                                                  OrderSide side, double amount,
                                                                  OrderType order_type)
    {
        if (token_id.empty())
            return Result<MarketPriceEstimate>::failure(
                estimate_error(SdkErrorCode::InvalidArgument, "token_id is required", "/book"));
        if (auto error = validate_estimate_request(side, amount, order_type))
            return Result<MarketPriceEstimate>::failure(std::move(*error));

        const auto tick_info = tick_size_result(token_id);
        if (!tick_info) return Result<MarketPriceEstimate>::failure(tick_info.error());
        const auto book = order_book_result(token_id);
        if (!book) return Result<MarketPriceEstimate>::failure(book.error());
        return polymarket::estimate_market_price(book.value(), side, amount,
                                                 tick_info.value().minimum_tick_size, order_type);
    }
} // namespace polymarket
