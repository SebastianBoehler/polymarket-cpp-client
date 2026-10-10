#include "user_stream_protocol.hpp"
#include "websocket_resilience.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <map>
#include <string>

// Feeds one raw WebSocket frame to every market and user stream parser.
// Rejected input must raise std::exception; accepted book updates must leave
// each book with valid, ordered levels.
namespace
{
    using polymarket::Orderbook;
    using polymarket::PriceLevel;

    bool valid_level(const PriceLevel &level)
    {
        return std::isfinite(level.price) && std::isfinite(level.size) && level.price > 0.0 &&
               level.price < 1.0 && level.size >= 0.0;
    }

    void require_valid_book(const Orderbook &book)
    {
        const auto descending = [](const PriceLevel &left, const PriceLevel &right)
        { return left.price > right.price; };
        const auto ascending = [](const PriceLevel &left, const PriceLevel &right)
        { return left.price < right.price; };
        if (!std::all_of(book.bids.begin(), book.bids.end(), valid_level) ||
            !std::all_of(book.asks.begin(), book.asks.end(), valid_level) ||
            !std::is_sorted(book.bids.begin(), book.bids.end(), descending) ||
            !std::is_sorted(book.asks.begin(), book.asks.end(), ascending))
        {
            std::abort();
        }
    }

    template <typename Parse> void parse_or_reject(Parse parse)
    {
        try
        {
            parse();
        }
        catch (const std::exception &)
        {
        }
    }
} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size)
{
    namespace detail = polymarket::detail;
    const std::string message(reinterpret_cast<const char *>(data), size);

    parse_or_reject([&] { (void)detail::parse_typed_message(message); });
    parse_or_reject([&] { (void)detail::parse_market_tick_size_change(message); });
    parse_or_reject([&] { (void)detail::parse_user_events(message); });
    parse_or_reject(
        [&]
        {
            std::map<std::string, Orderbook> books;
            for (const auto &event : detail::parse_market_book_events(message))
            {
                auto &book = books[event.asset_id];
                parse_or_reject([&] { detail::apply_market_book_event(book, event, 1); });
                require_valid_book(book);
            }
        });
    return 0;
}
