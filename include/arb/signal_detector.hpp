#pragma once

#include "arb/arb_types.hpp"
#include "orderbook.hpp"
#include <optional>

namespace polymarket::arb
{

    class SignalDetector
    {
    public:
        explicit SignalDetector(ArbConfig config);

        std::optional<OpportunitySignal> detect(const PreparedMarket &market,
                                                const Orderbook &yes_book,
                                                const Orderbook &no_book) const;

    private:
        ArbConfig config_;
    };

} // namespace polymarket::arb
