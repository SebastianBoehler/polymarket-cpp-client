#include "polymarket/clob_client.hpp"
#include "order_execution.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>

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
            const auto cached = get_tick_size(params.token_id);
            if (!cached || cached->minimum_tick_size.empty())
                return Result<OrderResponse>::failure({SdkErrorCode::HttpTransport,
                                                       "could not resolve market tick size",
                                                       "/tick-size", 0, "", "", true});
            if (!on_tick_grid(params.price, cached->minimum_tick_size))
                clear_market_metadata_cache(params.token_id);
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
} // namespace polymarket
