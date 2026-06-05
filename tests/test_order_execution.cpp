#include "order_execution.hpp"

#include <cmath>
#include <iostream>
#include <string>

using namespace polymarket;

namespace
{
    constexpr const char *kSigner = "0x1111111111111111111111111111111111111111";
    constexpr const char *kFunder = "0x2222222222222222222222222222222222222222";
    constexpr const char *kStandardExchange = "0x3333333333333333333333333333333333333333";
    constexpr const char *kNegRiskExchange = "0x4444444444444444444444444444444444444444";
    constexpr const char *kZeroBytes32 = "0x0000000000000000000000000000000000000000000000000000000000000000";
    constexpr const char *kZeroAddress = "0x0000000000000000000000000000000000000000";

    bool expect_true(const std::string &name, bool value)
    {
        if (value)
        {
            return true;
        }
        std::cerr << "failed: " << name << "\n";
        return false;
    }

    bool expect_close(const std::string &name, double actual, double expected)
    {
        return expect_true(name, std::fabs(actual - expected) < 0.0000001);
    }

    bool expect_equal(const std::string &name, const std::string &actual, const std::string &expected)
    {
        if (actual == expected)
        {
            return true;
        }
        std::cerr << name << " mismatch\n"
                  << "  expected: " << expected << "\n"
                  << "  actual:   " << actual << "\n";
        return false;
    }

    SignedOrder sample_order(const std::string &salt, int side)
    {
        SignedOrder order;
        order.salt = salt;
        order.maker = kFunder;
        order.signer = kSigner;
        order.taker = "";
        order.token_id = "123456789";
        order.maker_amount = "4200000";
        order.taker_amount = "10000000";
        order.expiration = "0";
        order.side = side;
        order.signature_type = static_cast<int>(SignatureType::POLY_PROXY);
        order.timestamp = "1713398400000";
        order.metadata = kZeroBytes32;
        order.builder = kZeroBytes32;
        order.signature = "0xabc";
        return order;
    }
}

int main()
{
    const auto buy_amounts = detail::calculate_limit_order_amounts(OrderSide::BUY, 0.42, 10.0);
    if (!expect_close("buy maker amount", buy_amounts.maker, 4.2) ||
        !expect_close("buy taker amount", buy_amounts.taker, 10.0))
    {
        return 1;
    }

    const auto reported_gtc = detail::calculate_limit_order_amounts(OrderSide::BUY, 0.1700000850000425, 5.8823);
    if (!expect_close("reported GTC maker amount", reported_gtc.maker, 0.9996) ||
        !expect_close("reported GTC taker amount", reported_gtc.taker, 5.88) ||
        !expect_equal("reported GTC maker wei", to_wei(reported_gtc.maker, 6), "999600") ||
        !expect_equal("reported GTC taker wei", to_wei(reported_gtc.taker, 6), "5880000"))
    {
        return 1;
    }

    const auto reported_fak = detail::calculate_market_order_amounts(
        OrderSide::BUY,
        1.0,
        0.1700000850000425,
        detail::rounding_config_for_tick_size("0.01"));
    if (!expect_close("reported FAK maker amount", reported_fak.maker, 1.0) ||
        !expect_close("reported FAK taker amount", reported_fak.taker, 5.8823) ||
        !expect_equal("reported FAK maker wei", to_wei(reported_fak.maker, 6), "1000000") ||
        !expect_equal("reported FAK taker wei", to_wei(reported_fak.taker, 6), "5882300"))
    {
        return 1;
    }

    const auto sell_amounts = detail::calculate_limit_order_amounts(OrderSide::SELL, 0.42, 10.0);
    if (!expect_close("sell maker amount", sell_amounts.maker, 10.0) ||
        !expect_close("sell taker amount", sell_amounts.taker, 4.2))
    {
        return 1;
    }

    detail::OrderExecutionContext context{
        kSigner,
        "",
        SignatureType::EOA,
        kStandardExchange,
        kNegRiskExchange};

    if (!expect_equal("EOA maker", context.maker_address(), kSigner) ||
        !expect_equal("EOA signer", context.signer_for_order(), kSigner) ||
        !expect_equal("standard exchange", context.exchange_for(false), kStandardExchange) ||
        !expect_equal("neg risk exchange", context.exchange_for(true), kNegRiskExchange))
    {
        return 1;
    }

    context.funder_address = kFunder;
    context.signature_type = SignatureType::POLY_PROXY;
    if (!expect_equal("proxy maker", context.maker_address(), kFunder) ||
        !expect_equal("proxy signer", context.signer_for_order(), kSigner))
    {
        return 1;
    }

    context.signature_type = SignatureType::POLY_1271;
    if (!expect_equal("1271 signer", context.signer_for_order(), kFunder))
    {
        return 1;
    }

    const auto payload = detail::order_payload_json(sample_order("123456789", 0), "owner-key", "GTC");
    if (!expect_true("payload has order", payload.contains("order")) ||
        !expect_true("salt is numeric", payload["order"]["salt"].is_number_integer()) ||
        !expect_equal("taker defaults to zero address", payload["order"]["taker"], kZeroAddress) ||
        !expect_equal("side string", payload["order"]["side"], "BUY") ||
        !expect_equal("owner", payload["owner"], "owner-key") ||
        !expect_equal("order type", payload["orderType"], "GTC") ||
        !expect_true("post only false", payload["postOnly"] == false) ||
        !expect_true("defer exec false", payload["deferExec"] == false) ||
        !expect_true("nonce omitted", !payload["order"].contains("nonce")) ||
        !expect_true("fee omitted", !payload["order"].contains("feeRateBps")))
    {
        return 1;
    }

    std::vector<BatchOrderEntry> batch{
        {sample_order("1", 0), OrderType::GTC},
        {sample_order("2", 1), OrderType::FAK}};
    const auto batch_payload = detail::batch_order_payload_json(
        batch,
        "owner-key",
        [](OrderType type)
        {
            return type == OrderType::GTC ? "GTC" : "FAK";
        });

    if (!expect_true("batch is array", batch_payload.is_array()) ||
        !expect_true("batch size", batch_payload.size() == 2) ||
        !expect_equal("batch second side", batch_payload[1]["order"]["side"], "SELL") ||
        !expect_equal("batch second type", batch_payload[1]["orderType"], "FAK"))
    {
        return 1;
    }

    return 0;
}
