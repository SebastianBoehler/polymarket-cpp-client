#pragma once

#include "clob_client.hpp"
#include <nlohmann/json.hpp>
#include <cstdint>
#include <functional>

namespace polymarket::detail
{
    struct OrderExecutionContext
    {
        std::string signer_address;
        std::string funder_address;
        SignatureType signature_type{SignatureType::EOA};
        std::string standard_exchange_address;
        std::string neg_risk_exchange_address;

        std::string maker_address() const;
        std::string signer_for_order() const;
        std::string exchange_for(bool neg_risk) const;
    };

    struct OrderAmounts
    {
        std::uint64_t maker{0};
        std::uint64_t taker{0};
    };

    struct OrderRoundingConfig
    {
        int price_decimals{0};
        int size_decimals{0};
        int amount_decimals{0};
        std::uint64_t tick_units{0};
    };

    struct ValidatedOrderPrice
    {
        std::uint64_t units{0};
        std::uint64_t scale{0};
        OrderRoundingConfig rounding;
    };

    OrderRoundingConfig rounding_config_for_tick_size(const std::string &tick_size);
    ValidatedOrderPrice validate_order_price(double price,
                                             const std::string &tick_size);
    OrderAmounts calculate_limit_order_amounts(OrderSide side, double price, double size);
    OrderAmounts calculate_limit_order_amounts(OrderSide side,
                                               double price,
                                               double size,
                                               const OrderRoundingConfig &rounding);
    OrderAmounts calculate_limit_order_amounts(OrderSide side,
                                               double size,
                                               const ValidatedOrderPrice &price);
    OrderAmounts calculate_market_order_amounts(OrderSide side,
                                                double amount,
                                                double price,
                                                const OrderRoundingConfig &rounding);
    OrderAmounts calculate_market_order_amounts(OrderSide side,
                                                double amount,
                                                const ValidatedOrderPrice &price);
    double calculate_market_price(const Orderbook &book,
                                  OrderSide side,
                                  double amount,
                                  OrderType order_type,
                                  const std::string &tick_size);
    nlohmann::json signed_order_json(const SignedOrder &order);
    nlohmann::json order_payload_json(const SignedOrder &order,
                                      const std::string &owner,
                                      const std::string &order_type,
                                      bool post_only = false,
                                      bool defer_exec = false);
    nlohmann::json batch_order_payload_json(const std::vector<BatchOrderEntry> &orders,
                                           const std::string &owner,
                                           const std::function<std::string(OrderType)> &order_type_to_string);
}
