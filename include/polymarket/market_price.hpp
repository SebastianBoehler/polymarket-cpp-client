#pragma once

#include "polymarket/clob_types.hpp"
#include "polymarket/order_signer.hpp"
#include "polymarket/sdk_error.hpp"
#include "polymarket/types.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace polymarket
{
    // Simulated fill of a market order against one order book snapshot.
    // Amounts use 6-decimal base units, like signed order amounts.
    struct MarketPriceEstimate
    {
        // Deepest level the order reaches; create_market_order signs at this price.
        double price{0.0};
        // Collateral per share across the simulated fill.
        double average_price{0.0};
        // Shares bought or sold, and collateral paid or received. Each level
        // is rounded against the caller by less than one base unit.
        std::uint64_t share_units{0};
        std::uint64_t collateral_units{0};
        // Price levels the fill touches.
        std::size_t levels{0};
        // False when the book cannot absorb the whole amount; a FAK order
        // then fills only the simulated part.
        bool fully_fillable{false};
    };

    // Walks `book` for a market order of `amount` (collateral for BUY, shares
    // for SELL) without network access, so a book from OrderbookManager can be
    // used directly. Levels may be listed best-first or best-last.
    // FOK fails with InsufficientLiquidity when the book cannot fill the whole
    // amount; FAK returns the partial estimate.
    Result<MarketPriceEstimate> estimate_market_price(const Orderbook &book, OrderSide side,
                                                      double amount, const std::string &tick_size,
                                                      OrderType order_type = OrderType::FOK);
} // namespace polymarket
