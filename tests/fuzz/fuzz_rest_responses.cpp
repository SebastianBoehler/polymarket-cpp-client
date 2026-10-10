#include "clob_client_internal.hpp"
#include "polymarket/market_fetcher.hpp"
#include "rest_orderbook_parsing.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <string>
#include <vector>

// Feeds one raw REST response body to every CLOB response parser. Rejected
// input must raise std::exception; an accepted order book must hold valid
// levels with no repeated price on either side.
namespace
{
    using polymarket::PriceLevel;

    bool valid_side(const std::vector<PriceLevel> &levels)
    {
        std::vector<double> prices;
        prices.reserve(levels.size());
        for (const auto &level : levels)
        {
            if (!std::isfinite(level.price) || !std::isfinite(level.size) || level.price <= 0.0 ||
                level.price >= 1.0 || level.size < 0.0)
            {
                return false;
            }
            prices.push_back(level.price);
        }
        std::sort(prices.begin(), prices.end());
        return std::adjacent_find(prices.begin(), prices.end()) == prices.end();
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
    const std::string body(reinterpret_cast<const char *>(data), size);

    parse_or_reject(
        [&]
        {
            const auto book = detail::parse_rest_orderbook_json(body);
            if (book.asset_id.empty() || !valid_side(book.bids) || !valid_side(book.asks))
            {
                std::abort();
            }
        });
    parse_or_reject([&] { (void)detail::parse_open_order_json(body); });
    parse_or_reject([&] { (void)detail::parse_open_orders_json(body); });
    parse_or_reject([&] { (void)detail::parse_open_order_page_json(body); });
    parse_or_reject([&] { (void)detail::parse_order_response_json_strict(body); });
    parse_or_reject([&] { (void)detail::parse_cancellation_response_json(body); });
    parse_or_reject([&] { (void)detail::parse_trade_page_json(body); });
    parse_or_reject([&] { (void)detail::parse_clob_markets_json(body); });
    parse_or_reject([&] { (void)detail::parse_clob_market_page_json(body); });
    return 0;
}
