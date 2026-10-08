#include "order_execution.hpp"

#include <stdexcept>

namespace polymarket::detail
{
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
