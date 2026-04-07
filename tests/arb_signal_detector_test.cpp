#include "arb/signal_detector.hpp"
#include <cassert>
#include <iostream>

using namespace polymarket;
using namespace polymarket::arb;

namespace
{
    PreparedMarket make_market()
    {
        PreparedMarket market;
        market.market.condition_id = "condition-1";
        market.market.token_yes = "yes-token";
        market.market.token_no = "no-token";
        market.metadata.tick_size = 0.01;
        market.metadata.neg_risk = true;
        return market;
    }

    Orderbook make_book(const std::string &asset_id,
                        double ask_price,
                        double ask_size,
                        uint64_t timestamp_ns)
    {
        Orderbook book;
        book.asset_id = asset_id;
        book.timestamp_ns = timestamp_ns;
        book.asks.push_back({ask_price, ask_size});
        book.bids.push_back({ask_price - 0.01, ask_size});
        return book;
    }
} // namespace

int main()
{
    ArbConfig config;
    config.trigger_threshold = 0.99;
    config.per_leg_size_usdc = 5.0;
    config.slippage_buffer = 0.005;
    config.max_book_age_ms = 1000;
    config.minimum_visible_depth_ratio = 1.0;

    SignalDetector detector(config);
    const auto market = make_market();
    const auto timestamp = now_ns();

    const auto valid_yes = make_book("yes-token", 0.47, 20.0, timestamp);
    const auto valid_no = make_book("no-token", 0.47, 20.0, timestamp);
    const auto signal = detector.detect(market, valid_yes, valid_no);
    assert(signal.has_value());
    assert(signal->adjusted_yes_price == 0.48);
    assert(signal->adjusted_no_price == 0.48);

    const auto stale_yes = make_book("yes-token", 0.47, 20.0, timestamp - 2000000000ULL);
    assert(!detector.detect(market, stale_yes, valid_no).has_value());

    const auto shallow_no = make_book("no-token", 0.47, 5.0, timestamp);
    assert(!detector.detect(market, valid_yes, shallow_no).has_value());

    std::cout << "arb_signal_detector_test passed\n";
    return 0;
}
