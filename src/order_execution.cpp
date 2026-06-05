#include "order_execution.hpp"

#include <cmath>
#include <stdexcept>

namespace polymarket::detail
{
    namespace
    {
        constexpr const char *ZERO_ADDRESS = "0x0000000000000000000000000000000000000000";

        double decimal_scale(int decimals)
        {
            double scale = 1.0;
            for (int i = 0; i < decimals; ++i)
            {
                scale *= 10.0;
            }
            return scale;
        }

        double round_down(double value, int decimals)
        {
            const double scale = decimal_scale(decimals);
            return std::floor(value * scale) / scale;
        }

        double round_up(double value, int decimals)
        {
            const double scale = decimal_scale(decimals);
            return std::ceil(value * scale) / scale;
        }

        double round_nearest(double value, int decimals)
        {
            const double scale = decimal_scale(decimals);
            return std::round(value * scale) / scale;
        }

        double round_order_amount(double value, int decimals)
        {
            return round_down(round_up(value, decimals + 4), decimals);
        }
    }

    std::string OrderExecutionContext::maker_address() const
    {
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

    OrderAmounts calculate_limit_order_amounts(OrderSide side, double price, double size)
    {
        return calculate_limit_order_amounts(side, price, size, rounding_config_for_tick_size("0.01"));
    }

    OrderRoundingConfig rounding_config_for_tick_size(const std::string &tick_size)
    {
        if (tick_size == "0.1")
        {
            return {1, 2, 3};
        }
        if (tick_size == "0.01")
        {
            return {2, 2, 4};
        }
        if (tick_size == "0.001")
        {
            return {3, 2, 5};
        }
        if (tick_size == "0.0001")
        {
            return {4, 2, 6};
        }
        throw std::invalid_argument("unsupported tick size: " + tick_size);
    }

    OrderAmounts calculate_limit_order_amounts(OrderSide side,
                                               double price,
                                               double size,
                                               const OrderRoundingConfig &rounding)
    {
        const double raw_price = round_nearest(price, rounding.price_decimals);
        if (side == OrderSide::BUY)
        {
            const double raw_taker = round_down(size, rounding.size_decimals);
            return {round_order_amount(raw_taker * raw_price, rounding.amount_decimals), raw_taker};
        }

        const double raw_maker = round_down(size, rounding.size_decimals);
        return {raw_maker, round_order_amount(raw_maker * raw_price, rounding.amount_decimals)};
    }

    OrderAmounts calculate_market_order_amounts(OrderSide side,
                                                double amount,
                                                double price,
                                                const OrderRoundingConfig &rounding)
    {
        const double raw_price = round_down(price, rounding.price_decimals);
        const double raw_maker = round_down(amount, rounding.size_decimals);
        if (side == OrderSide::BUY)
        {
            return {raw_maker, round_order_amount(raw_maker / raw_price, rounding.amount_decimals)};
        }
        return {raw_maker, round_order_amount(raw_maker * raw_price, rounding.amount_decimals)};
    }

    nlohmann::json signed_order_json(const SignedOrder &order)
    {
        return {
            {"salt", std::stoll(order.salt)},
            {"maker", order.maker},
            {"signer", order.signer},
            {"taker", order.taker.empty() ? ZERO_ADDRESS : order.taker},
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
