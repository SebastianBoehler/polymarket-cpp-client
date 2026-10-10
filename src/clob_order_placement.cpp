#include "polymarket/clob_client.hpp"
#include "order_execution.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace polymarket
{
    namespace
    {
        // Same buffer as py-sdk: the server rejects GTD orders that expire too
        // soon, so leave room for latency and clock skew.
        constexpr std::uint64_t min_expiration_lead_seconds = 180;

        Result<OrderResponse> invalid_order(const std::string &message)
        {
            return Result<OrderResponse>::failure(
                {SdkErrorCode::InvalidArgument, message, "/order", 0, "", "", false});
        }

        std::uint64_t unix_now_seconds()
        {
            const auto now = std::chrono::system_clock::now().time_since_epoch();
            return static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::seconds>(now).count());
        }

        bool on_tick_grid(double price, const std::string &tick_size)
        {
            try
            {
                (void)detail::validate_order_price(price, tick_size);
                return true;
            }
            catch (const std::exception &)
            {
                return false;
            }
        }

        bool levels_on_tick_grid(const std::vector<PriceLevel> &levels,
                                 const std::string &tick_size)
        {
            return std::all_of(levels.begin(), levels.end(), [&](const PriceLevel &level)
                               { return on_tick_grid(level.price, tick_size); });
        }
    } // namespace

    Result<OrderResponse> ClobClient::place_limit_order(const PlaceLimitOrderParams &params)
    {
        if (!is_authenticated())
            return Result<OrderResponse>::failure(
                make_auth_error("Client not authenticated", "/order"));
        if (params.token_id.empty()) return invalid_order("token_id is required");
        if (!std::isfinite(params.price) || params.price <= 0.0 || params.price >= 1.0)
            return invalid_order("limit price must be between 0 and 1");
        if (!std::isfinite(params.size) || params.size <= 0.0)
            return invalid_order("limit size must be finite and positive");
        if (params.expiration &&
            *params.expiration < unix_now_seconds() + min_expiration_lead_seconds)
            return invalid_order("expiration must be at least 180 seconds in the future");

        if (params.tick_size.empty())
        {
            const auto cached = tick_size_result(params.token_id);
            if (!cached) return Result<OrderResponse>::failure(cached.error());
            if (!on_tick_grid(params.price, cached.value().minimum_tick_size))
                evict_tick_size(params.token_id);
        }

        CreateOrderParams order;
        order.token_id = params.token_id;
        order.price = params.price;
        order.size = params.size;
        order.side = params.side;
        order.tick_size = params.tick_size;
        order.expiration = params.expiration ? std::to_string(*params.expiration) : "0";
        order.metadata = params.metadata;
        order.builder_code = params.builder_code;
        order.neg_risk = params.neg_risk;

        const auto signed_order = create_order_result(order);
        if (!signed_order) return Result<OrderResponse>::failure(signed_order.error());
        return post_signed_order(signed_order.value(),
                                 params.expiration ? OrderType::GTD : OrderType::GTC,
                                 params.post_only);
    }

    Result<OrderResponse> ClobClient::place_market_order(const PlaceMarketOrderParams &params)
    {
        if (!is_authenticated())
            return Result<OrderResponse>::failure(
                make_auth_error("Client not authenticated", "/order"));
        if (params.token_id.empty()) return invalid_order("token_id is required");
        if (params.order_type != OrderType::FAK && params.order_type != OrderType::FOK)
            return invalid_order("Market orders require FAK or FOK");
        if (!std::isfinite(params.amount) || params.amount <= 0.0)
            return invalid_order("market order amount must be finite and positive");
        if (params.worst_price && (!std::isfinite(*params.worst_price) ||
                                   *params.worst_price <= 0.0 || *params.worst_price >= 1.0))
            return invalid_order("worst price must be between 0 and 1");

        const bool uses_market_tick = params.tick_size.empty();
        std::string tick_size = params.tick_size;
        if (uses_market_tick)
        {
            const auto info = tick_size_result(params.token_id);
            if (!info) return Result<OrderResponse>::failure(info.error());
            tick_size = info.value().minimum_tick_size;
        }

        CreateMarketOrderParams order;
        order.token_id = params.token_id;
        order.amount = params.amount;
        order.side = params.side;
        order.tick_size = params.tick_size;
        order.metadata = params.metadata;
        order.builder_code = params.builder_code;
        order.neg_risk = params.neg_risk;

        if (params.worst_price)
        {
            if (uses_market_tick && !on_tick_grid(*params.worst_price, tick_size))
                evict_tick_size(params.token_id);
            order.price = params.worst_price;
        }
        else
        {
            const auto book = order_book_result(params.token_id);
            if (!book) return Result<OrderResponse>::failure(book.error());
            auto estimate = polymarket::estimate_market_price(
                book.value(), params.side, params.amount, tick_size, params.order_type);
            // Book levels on a finer grid than the cached tick mean the tick changed.
            const auto &levels =
                params.side == OrderSide::BUY ? book.value().asks : book.value().bids;
            if (!estimate && estimate.error().code == SdkErrorCode::InvalidArgument &&
                uses_market_tick && !levels_on_tick_grid(levels, tick_size))
            {
                evict_tick_size(params.token_id);
                const auto refreshed = tick_size_result(params.token_id);
                if (!refreshed) return Result<OrderResponse>::failure(refreshed.error());
                tick_size = refreshed.value().minimum_tick_size;
                estimate = polymarket::estimate_market_price(
                    book.value(), params.side, params.amount, tick_size, params.order_type);
            }
            if (!estimate) return Result<OrderResponse>::failure(estimate.error());
            order.price = estimate.value().price;
        }

        const auto prepared = create_market_order_result(order, params.order_type);
        if (!prepared) return Result<OrderResponse>::failure(prepared.error());
        return post_signed_order(prepared.value().order, prepared.value().order_type, false);
    }
} // namespace polymarket
