#pragma once

#include "clob_client.hpp"
#include <nlohmann/json.hpp>
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
        double maker;
        double taker;
    };

    OrderAmounts calculate_limit_order_amounts(OrderSide side, double price, double size);
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
