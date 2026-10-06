#include "websocket_resilience.hpp"

#include <chrono>
#include <iostream>
#include <string>

namespace
{
    // Real CLOB identifiers are 66-77 characters, beyond small-string storage.
    const std::string condition_id =
        "0x13c6cc0af4836015fcfdab862cadc62b2117df300b69ba173416fcfe9110649b";
    const std::string token_id =
        "83782113303236477866335970881283179052006378592241188539196785830237452024272";

    std::string book_message(int levels_per_side)
    {
        std::string bids;
        std::string asks;
        for (int level = 0; level < levels_per_side; ++level)
        {
            const auto separator = level == 0 ? "" : ",";
            bids += separator + std::string(R"({"price":"0.)") + std::to_string(4800 - level * 10) +
                    R"(","size":")" + std::to_string(100 + level) + R"(.25"})";
            asks += separator + std::string(R"({"price":"0.)") + std::to_string(5200 + level * 10) +
                    R"(","size":")" + std::to_string(90 + level) + R"(.5"})";
        }
        return R"([{"event_type":"book","market":")" + condition_id + R"(","asset_id":")" +
               token_id + R"(","timestamp":"1782753357257","hash":"0xabc","bids":[)" + bids +
               R"(],"asks":[)" + asks + "]}]";
    }

    std::string price_change_message()
    {
        return R"({"event_type":"price_change","market":")" + condition_id +
               R"(","timestamp":"1782753357257","price_changes":[{"asset_id":")" + token_id +
               R"(","price":"0.4790","size":"250.5","side":"BUY","hash":"0xdef","best_bid":"0.48","best_ask":"0.52"},{"asset_id":")" +
               token_id +
               R"(","price":"0.5210","size":"0","side":"SELL","hash":"0x123","best_bid":"0.48","best_ask":"0.52"}]})";
    }

    // Parses and applies one message per iteration to a persistent book, the
    // steady-state work OrderbookManager does per frame.
    void run(const std::string &name, const std::string &message, int iterations,
             polymarket::Orderbook &book)
    {
        std::size_t levels = 0;
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < iterations; ++i)
        {
            for (const auto &event : polymarket::detail::parse_market_book_events(message))
            {
                polymarket::detail::apply_market_book_event(book, event, 1);
                levels += book.bids.size() + book.asks.size();
            }
        }
        const auto elapsed = std::chrono::steady_clock::now() - start;
        const auto elapsed_ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
        std::cout << "benchmark=ws_parse workload=" << name << " iterations=" << iterations
                  << " bytes=" << message.size()
                  << " avg_ns=" << static_cast<double>(elapsed_ns) / iterations
                  << " levels=" << levels << "\n";
    }
} // namespace

int main(int argc, char **argv)
{
    const int iterations = argc > 1 ? std::stoi(argv[1]) : 100000;
    polymarket::Orderbook book;
    run("book_100_levels", book_message(50), iterations / 10 + 1, book);
    run("price_change_2", price_change_message(), iterations, book);
    run("book_2_levels", book_message(1), iterations, book);
    return 0;
}
