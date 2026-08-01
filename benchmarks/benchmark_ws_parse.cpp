#include "websocket_resilience.hpp"

#include <chrono>
#include <iostream>

int main(int argc, char **argv)
{
    const int iterations = argc > 1 ? std::stoi(argv[1]) : 100000;
    const std::string message =
        R"([{"event_type":"book","market":"condition-1","asset_id":"token-1","timestamp":"1782753357257","bids":[{"price":"0.41","size":"100"}],"asks":[{"price":"0.42","size":"80"}]}])";

    auto start = std::chrono::steady_clock::now();
    std::size_t levels = 0;
    for (int i = 0; i < iterations; ++i)
    {
        const auto events = polymarket::detail::parse_market_book_events(message);
        polymarket::Orderbook book;
        for (const auto &event : events)
        {
            polymarket::detail::apply_market_book_event(book, event, 1);
            levels += book.bids.size() + book.asks.size();
        }
    }
    auto end = std::chrono::steady_clock::now();
    const auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    std::cout << "benchmark=ws_parse iterations=" << iterations
              << " total_us=" << elapsed_us
              << " avg_us=" << static_cast<double>(elapsed_us) / iterations
              << " levels=" << levels << "\n";
    return 0;
}
