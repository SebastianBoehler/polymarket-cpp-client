#include "order_execution.hpp"
#include "market_price_contracts.hpp"
#include "order_amount_properties.hpp"

#include <cmath>
#include <functional>
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

    bool expect_throws(const std::string &name, const std::function<void()> &action)
    {
        try
        {
            action();
        }
        catch (const std::exception &)
        {
            return true;
        }
        std::cerr << "failed: " << name << " did not throw\n";
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
    const auto tick_005 = detail::rounding_config_for_tick_size("0.005");
    const auto tick_0025 = detail::rounding_config_for_tick_size("0.0025");
    if (!expect_true("0.005 price decimals", tick_005.price_decimals == 3) ||
        !expect_true("0.005 amount decimals", tick_005.amount_decimals == 5) ||
        !expect_true("0.0025 price decimals", tick_0025.price_decimals == 4) ||
        !expect_true("0.0025 amount decimals", tick_0025.amount_decimals == 6))
    {
        return 1;
    }

    const auto half_even_price = detail::calculate_limit_order_amounts(
        OrderSide::BUY, 0.45, 2.0, detail::rounding_config_for_tick_size("0.1"));
    if (!expect_equal("Python-compatible half-even price rounding",
                      std::to_string(half_even_price.maker), "800000"))
    {
        return 1;
    }

    const auto half_cent_buy = detail::calculate_limit_order_amounts(
        OrderSide::BUY, 0.5126, 7.891, tick_005);
    const auto quarter_cent_sell = detail::calculate_limit_order_amounts(
        OrderSide::SELL, 0.5126, 7.891, tick_0025);
    const auto half_cent_market_buy = detail::calculate_market_order_amounts(
        OrderSide::BUY, 10.123, 0.5126, tick_005);
    const auto quarter_cent_market_sell = detail::calculate_market_order_amounts(
        OrderSide::SELL, 7.891, 0.5126, tick_0025);
    if (!expect_equal("0.005 market buy maker wei", std::to_string(half_cent_market_buy.maker), "10120000") ||
        !expect_equal("0.005 market buy taker wei", std::to_string(half_cent_market_buy.taker), "19765625") ||
        !expect_equal("0.0025 market sell maker wei", std::to_string(quarter_cent_market_sell.maker), "7890000") ||
        !expect_equal("0.0025 market sell taker wei", std::to_string(quarter_cent_market_sell.taker), "4044414") ||
        !expect_equal("0.005 limit buy maker wei", std::to_string(half_cent_buy.maker), "4047570") ||
        !expect_equal("0.005 limit buy taker wei", std::to_string(half_cent_buy.taker), "7890000") ||
        !expect_equal("0.0025 limit sell maker wei", std::to_string(quarter_cent_sell.maker), "7890000") ||
        !expect_equal("0.0025 limit sell taker wei", std::to_string(quarter_cent_sell.taker), "4044414"))
    {
        return 1;
    }

    Orderbook book;
    book.asks = {{0.70, 1.0}, {0.60, 3.0}, {0.50, 2.0}};
    book.bids = {{0.30, 1.0}, {0.40, 3.0}, {0.50, 2.0}};
    if (!expect_close("buy depth worst price",
                      detail::calculate_market_price(book, OrderSide::BUY, 2.0,
                                                     OrderType::FOK, "0.1"), 0.60) ||
        !expect_close("sell depth worst price",
                      detail::calculate_market_price(book, OrderSide::SELL, 4.0,
                                                     OrderType::FOK, "0.1"), 0.40) ||
        !expect_close("buy FAK uses book limit",
                      detail::calculate_market_price(book, OrderSide::BUY, 9.0,
                                                     OrderType::FAK, "0.1"), 0.70) ||
        !expect_close("sell FAK uses book limit",
                      detail::calculate_market_price(book, OrderSide::SELL, 9.0,
                                                     OrderType::FAK, "0.1"), 0.30) ||
        !expect_throws("buy FOK rejects shallow book", [&]
                       { detail::calculate_market_price(book, OrderSide::BUY, 9.0,
                                                        OrderType::FOK, "0.1"); }) ||
        !expect_throws("empty book rejects quote", [&]
                       { detail::calculate_market_price(Orderbook{}, OrderSide::BUY, 1.0,
                                                        OrderType::FAK, "0.1"); }))
    {
        return 1;
    }

    const auto buy_amounts = detail::calculate_limit_order_amounts(OrderSide::BUY, 0.42, 10.0);
    if (!expect_true("buy maker amount", buy_amounts.maker == 4200000) ||
        !expect_true("buy taker amount", buy_amounts.taker == 10000000))
    {
        return 1;
    }

    const auto reported_gtc = detail::calculate_limit_order_amounts(OrderSide::BUY, 0.1700000850000425, 5.8823);
    if (!expect_true("reported GTC maker amount", reported_gtc.maker == 999600) ||
        !expect_true("reported GTC taker amount", reported_gtc.taker == 5880000))
    {
        return 1;
    }

    const auto reported_fak = detail::calculate_market_order_amounts(
        OrderSide::BUY,
        1.0,
        0.1700000850000425,
        detail::rounding_config_for_tick_size("0.01"));
    if (!expect_true("reported FAK maker amount", reported_fak.maker == 999991) ||
        !expect_true("reported FAK taker amount", reported_fak.taker == 5882300))
    {
        return 1;
    }

    const auto sell_amounts = detail::calculate_limit_order_amounts(OrderSide::SELL, 0.42, 10.0);
    if (!expect_true("sell maker amount", sell_amounts.maker == 10000000) ||
        !expect_true("sell taker amount", sell_amounts.taker == 4200000))
    {
        return 1;
    }

    const auto decimal_boundary = detail::calculate_limit_order_amounts(
        OrderSide::BUY, 0.29, 5.29, detail::rounding_config_for_tick_size("0.01"));
    const auto exact_market_buy = detail::calculate_market_order_amounts(
        OrderSide::BUY, 1.06, 0.51, detail::rounding_config_for_tick_size("0.01"));
    const auto exact_market_maker = exact_market_buy.maker;
    const auto exact_market_taker = exact_market_buy.taker;
    if (!expect_equal("common-decimal size stays on cents",
                      std::to_string(decimal_boundary.taker), "5290000") ||
        !expect_equal("common-decimal price product stays exact",
                      std::to_string(decimal_boundary.maker), "1534100") ||
        !expect_true("market BUY signed ratio exactly matches price",
                     exact_market_maker * 100 == exact_market_taker * 51) ||
        !expect_true("market BUY maker stays within budget",
                     exact_market_maker <= 1060000))
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

    if (!order_amount_properties::run())
        return 1;
    if (!market_price_contracts::run())
        return 1;

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
        !expect_true("V2 payload omits V1-only taker", !payload["order"].contains("taker")) ||
        !expect_true("V2 payload has exactly the canonical signed fields",
                     payload["order"].size() == 13) ||
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
