#include "order_execution.hpp"
#include "decimal_math.hpp"

#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace polymarket::detail
{
    namespace
    {
        constexpr int depth_decimals = 6;
        constexpr std::uint64_t depth_scale = 1'000'000;

        struct AccumulatedNotional
        {
            std::uint64_t units{0};
            std::uint64_t fractional_units{0};
            bool saturated{false};

            bool covers(std::uint64_t target) const
            {
                return saturated || units >= target;
            }
        };

        struct AccumulatedSize
        {
            std::uint64_t units{0};
            bool saturated{false};

            bool covers(std::uint64_t target) const
            {
                return saturated || units >= target;
            }
        };

        std::uint64_t exact_depth_units(double value)
        {
            return exact_decimal_to_scaled_uint64(value, depth_decimals);
        }

        void add_notional(AccumulatedNotional &total,
                          std::uint64_t size_units,
                          std::uint64_t price_units)
        {
            if (total.saturated) return;

            const auto size_whole = size_units / depth_scale;
            const auto size_fraction = size_units % depth_scale;
            const auto fractional_product = size_fraction * price_units;
            const auto fractional_whole = fractional_product / depth_scale;
            const auto contribution_fraction = fractional_product % depth_scale;
            constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
            if (price_units != 0 &&
                size_whole > (maximum - fractional_whole) / price_units)
            {
                total.saturated = true;
                return;
            }
            const auto contribution_units =
                size_whole * price_units + fractional_whole;
            if (contribution_units > maximum - total.units)
            {
                total.saturated = true;
                return;
            }
            total.units += contribution_units;
            total.fractional_units += contribution_fraction;
            if (total.fractional_units < depth_scale) return;

            total.fractional_units -= depth_scale;
            if (total.units == maximum)
            {
                total.saturated = true;
                return;
            }
            ++total.units;
        }

        void add_size(AccumulatedSize &total, std::uint64_t size_units)
        {
            if (total.saturated) return;
            constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
            if (size_units > maximum - total.units)
            {
                total.saturated = true;
                return;
            }
            total.units += size_units;
        }
    }

    std::string OrderExecutionContext::maker_address() const
    {
        if (funder_address.empty() && signature_type != SignatureType::EOA)
            throw std::invalid_argument(
                "non-EOA signature types require a funder address");
        return funder_address.empty() ? signer_address : funder_address;
    }

    std::string OrderExecutionContext::signer_for_order() const
    {
        return signature_type == SignatureType::POLY_1271 ? maker_address() : signer_address;
    }

    std::string OrderExecutionContext::exchange_for(bool neg_risk) const
    {
        return neg_risk ? neg_risk_exchange_address : standard_exchange_address;
    }

    double calculate_market_price(const Orderbook &book,
                                  OrderSide side,
                                  double amount,
                                  OrderType order_type,
                                  const std::string &tick_size)
    {
        if (!std::isfinite(amount) || amount <= 0.0)
        {
            throw std::invalid_argument(
                "market order amount must be finite and positive");
        }
        const auto &levels = side == OrderSide::BUY ? book.asks : book.bids;
        if (levels.empty())
        {
            throw std::runtime_error("no matching orders");
        }

        AccumulatedNotional available_notional;
        AccumulatedSize available_size;
        for (auto level = levels.rbegin(); level != levels.rend(); ++level)
        {
            if (!std::isfinite(level->price) || !std::isfinite(level->size) ||
                level->price <= 0.0 || level->price >= 1.0 || level->size < 0.0)
            {
                throw std::invalid_argument("orderbook level is invalid");
            }
            const auto size_units = exact_depth_units(level->size);
            const auto price_units = exact_depth_units(level->price);
            if (price_units == 0 || price_units >= depth_scale)
                throw std::invalid_argument("orderbook level is invalid");
            const auto candidate_price = validate_order_price(
                level->price, tick_size);
            const auto candidate_amounts = calculate_market_order_amounts(
                side, amount, candidate_price);
            if (side == OrderSide::BUY)
                add_notional(available_notional, size_units, price_units);
            else
                add_size(available_size, size_units);
            if ((side == OrderSide::BUY &&
                 available_notional.covers(candidate_amounts.maker)) ||
                (side == OrderSide::SELL &&
                 available_size.covers(candidate_amounts.maker)))
            {
                return level->price;
            }
        }

        if (order_type == OrderType::FOK)
        {
            throw std::runtime_error("insufficient orderbook depth for FOK order");
        }
        return levels.front().price;
    }

    nlohmann::json signed_order_json(const SignedOrder &order)
    {
        return {
            {"salt", std::stoll(order.salt)},
            {"maker", order.maker},
            {"signer", order.signer},
            {"tokenId", order.token_id},
            {"makerAmount", order.maker_amount},
            {"takerAmount", order.taker_amount},
            {"expiration", order.expiration},
            {"side", order.side == 0 ? "BUY" : "SELL"},
            {"signatureType", order.signature_type},
            {"timestamp", order.timestamp},
            {"metadata", order.metadata},
            {"builder", order.builder},
            {"signature", order.signature}};
    }

    nlohmann::json order_payload_json(const SignedOrder &order,
                                      const std::string &owner,
                                      const std::string &order_type,
                                      bool post_only,
                                      bool defer_exec)
    {
        return {
            {"order", signed_order_json(order)},
            {"owner", owner},
            {"orderType", order_type},
            {"deferExec", defer_exec},
            {"postOnly", post_only}};
    }

    nlohmann::json batch_order_payload_json(const std::vector<BatchOrderEntry> &orders,
                                           const std::string &owner,
                                           const std::function<std::string(OrderType)> &order_type_to_string)
    {
        auto body = nlohmann::json::array();
        for (const auto &entry : orders)
        {
            body.push_back(order_payload_json(entry.order, owner, order_type_to_string(entry.order_type)));
        }
        return body;
    }
}
