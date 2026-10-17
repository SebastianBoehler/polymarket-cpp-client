#include "clob_client_internal.hpp"
#include "data_parsing.hpp"
#include "gamma_parsing.hpp"
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

// Feeds one raw REST response body to every CLOB, Gamma and Data API response parser. Rejected
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
    parse_or_reject(
        [&]
        {
            if (detail::parse_gamma_market_response(body).id.empty()) std::abort();
        });
    parse_or_reject(
        [&]
        {
            const auto event = detail::parse_gamma_event_response(body);
            if (event.id.empty()) std::abort();
            for (const auto &market : event.markets)
            {
                if (market.id.empty()) std::abort();
            }
        });
    parse_or_reject([&] { (void)detail::parse_gamma_market_keyset_page(body); });
    parse_or_reject([&] { (void)detail::parse_gamma_event_keyset_page(body); });
    parse_or_reject([&] { (void)detail::parse_gamma_tag_references(body); });
    parse_or_reject([&] { (void)detail::parse_gamma_tag_response(body); });
    parse_or_reject([&] { (void)detail::parse_gamma_related_tags(body); });
    parse_or_reject(
        [&]
        {
            if (detail::parse_gamma_series_response(body).id.empty()) std::abort();
        });
    parse_or_reject([&] { (void)detail::parse_gamma_series_list(body); });
    parse_or_reject([&] { (void)detail::parse_gamma_sports(body); });
    parse_or_reject([&] { (void)detail::parse_gamma_sports_market_types(body); });
    parse_or_reject([&] { (void)detail::parse_gamma_teams(body); });
    parse_or_reject([&] { (void)detail::parse_gamma_comments(body); });
    parse_or_reject([&] { (void)detail::parse_gamma_comment_keyset_page(body); });
    parse_or_reject(
        [&]
        {
            const auto page = detail::parse_gamma_comment_root_page(body);
            if (!page.counted || *page.counted > page.items.size()) std::abort();
        });
    parse_or_reject([&] { (void)detail::parse_gamma_public_profile(body); });
    parse_or_reject(
        [&]
        {
            if (detail::parse_gamma_search_page(body).items.size() != 1) std::abort();
        });
    parse_or_reject(
        [&]
        {
            const auto page = detail::parse_data_trade_page(body);
            for (const auto &trade : page.items)
            {
                if (trade.condition_id.empty() || (trade.side != "BUY" && trade.side != "SELL"))
                    std::abort();
            }
        });
    parse_or_reject(
        [&]
        {
            const auto page = detail::parse_data_activity_page(body);
            for (const auto &activity : page.items)
            {
                if (activity.known && !activity.timestamp) std::abort();
            }
        });
    parse_or_reject(
        [&]
        {
            const auto page = detail::parse_data_position_page(body);
            for (const auto &position : page.items)
            {
                if (position.condition_id.empty() || position.status.empty()) std::abort();
            }
        });
    parse_or_reject([&] { (void)detail::parse_data_portfolio_value(body); });
    parse_or_reject([&] { (void)detail::parse_data_user_stats(body); });
    parse_or_reject([&] { (void)detail::parse_data_pnl_series(body); });
    parse_or_reject([&] { (void)detail::parse_data_user_volume(body); });
    parse_or_reject(
        [&]
        {
            for (const auto &group : detail::parse_data_holders_page(body).items)
            {
                if (group.asset_id.empty()) std::abort();
            }
        });
    parse_or_reject([&] { (void)detail::parse_data_open_interests(body); });
    parse_or_reject([&] { (void)detail::parse_data_live_volume(body); });
    parse_or_reject([&] { (void)detail::parse_data_price_history_page(body); });
    parse_or_reject(
        [&]
        {
            for (const auto &resolution : detail::parse_data_resolutions(body))
            {
                if (resolution.status.empty() || resolution.last_updated_at <= 0) std::abort();
            }
        });
    return 0;
}
