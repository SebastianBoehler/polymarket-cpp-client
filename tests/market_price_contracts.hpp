#pragma once

#include "order_execution.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace market_price_contracts
{
    inline bool run()
    {
        using namespace polymarket;
        Orderbook book;
        book.asks = {{0.9, 1.0}, {0.7, 1.0}, {0.1, 1.0}};
        book.bids = {{0.3, 1.0}, {0.4, 0.7}, {0.5, 0.1}};
        const auto buy = detail::calculate_market_price(
            book, OrderSide::BUY, 0.8, OrderType::FOK, "0.1");
        const auto sell = detail::calculate_market_price(
            book, OrderSide::SELL, 0.8, OrderType::FOK, "0.1");
        const auto buy_above = detail::calculate_market_price(
            book, OrderSide::BUY, 0.800001, OrderType::FOK, "0.1");
        const auto sell_above = detail::calculate_market_price(
            book, OrderSide::SELL, 0.800001, OrderType::FOK, "0.1");
        const auto buy_amounts = detail::calculate_market_order_amounts(
            OrderSide::BUY, 0.800001,
            detail::validate_order_price(buy_above, "0.1"));
        const auto sell_amounts = detail::calculate_market_order_amounts(
            OrderSide::SELL, 0.800001,
            detail::validate_order_price(sell_above, "0.1"));

        Orderbook exact_maker_book;
        exact_maker_book.asks = {{0.7, 1.14285}};
        const auto exact_maker_buy = detail::calculate_market_price(
            exact_maker_book, OrderSide::BUY, 0.8, OrderType::FOK, "0.1");

        Orderbook large_book;
        large_book.asks = {{0.9999, 10'000'000'000'000.0}};
        const auto large_buy = detail::calculate_market_price(
            large_book, OrderSide::BUY, 9'999'000'000'000.0,
            OrderType::FOK, "0.0001");

        Orderbook saturating_book;
        saturating_book.asks = {
            {0.9999, 1'000'000'000'000.0},
            {0.99, 18'000'000'000'000.0}};
        saturating_book.bids = {
            {0.4, 10'000'000'000'000.0},
            {0.5, 10'000'000'000'000.0}};
        const auto saturating_buy = detail::calculate_market_price(
            saturating_book, OrderSide::BUY, 18'000'000'000'000.0,
            OrderType::FOK, "0.0001");
        const auto saturating_sell = detail::calculate_market_price(
            saturating_book, OrderSide::SELL, 18'000'000'000'000.0,
            OrderType::FOK, "0.1");

        bool rejected_excess_precision = false;
        try
        {
            Orderbook invalid_book;
            invalid_book.asks = {{0.5, 1.0000001}};
            (void)detail::calculate_market_price(
                invalid_book, OrderSide::BUY, 0.5, OrderType::FOK, "0.01");
        }
        catch (const std::invalid_argument &)
        {
            rejected_excess_precision = true;
        }
        if (std::abs(buy - 0.7) > 1e-12 ||
            std::abs(sell - 0.4) > 1e-12 ||
            std::abs(buy_above - 0.7) > 1e-12 ||
            std::abs(sell_above - 0.4) > 1e-12 ||
            buy_amounts.maker != 799'995 ||
            buy_amounts.taker != 1'142'850 ||
            sell_amounts.maker != 800'000 ||
            sell_amounts.taker != 320'000 ||
            std::abs(exact_maker_buy - 0.7) > 1e-12 ||
            std::abs(large_buy - 0.9999) > 1e-12 ||
            std::abs(saturating_buy - 0.9999) > 1e-12 ||
            std::abs(saturating_sell - 0.4) > 1e-12 ||
            !rejected_excess_precision)
        {
            std::cerr << "exact cumulative market depth selected a worse level\n";
            return false;
        }
        return true;
    }
}
