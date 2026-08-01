#include "websocket_resilience.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <unordered_set>

using json = nlohmann::json;

namespace polymarket::detail
{
    namespace
    {
        double json_number(const json &value)
        {
            double number = 0.0;
            if (value.is_string())
            {
                const auto text = value.get<std::string>();
                std::size_t parsed = 0;
                number = std::stod(text, &parsed);
                if (parsed != text.size())
                {
                    throw std::invalid_argument("orderbook number has trailing data");
                }
            }
            else
            {
                number = value.get<double>();
            }
            if (!std::isfinite(number))
            {
                throw std::invalid_argument("orderbook number must be finite");
            }
            return number;
        }

        PriceLevel parse_level(const json &level)
        {
            PriceLevel parsed{json_number(level.at("price")),
                              json_number(level.at("size"))};
            if (parsed.price <= 0.0 || parsed.price >= 1.0 || parsed.size < 0.0)
            {
                throw std::invalid_argument("orderbook level is outside the market range");
            }
            return parsed;
        }

        void validate_level(const PriceLevel &level)
        {
            if (!std::isfinite(level.price) || !std::isfinite(level.size) ||
                level.price <= 0.0 || level.price >= 1.0 || level.size < 0.0)
            {
                throw std::invalid_argument("orderbook level is invalid");
            }
        }

        void parse_snapshot(const json &message, MarketBookEvent &event)
        {
            if (!message.contains("bids") || !message["bids"].is_array() ||
                !message.contains("asks") || !message["asks"].is_array())
            {
                throw std::invalid_argument(
                    "book snapshot requires bids and asks arrays");
            }

            const auto parse_side = [](const json &levels,
                                       std::vector<PriceLevel> &output)
            {
                std::unordered_set<double> prices;
                output.reserve(levels.size());
                for (const auto &level : levels)
                {
                    if (!level.is_object())
                    {
                        throw std::invalid_argument(
                            "orderbook level must be an object");
                    }
                    const auto parsed = parse_level(level);
                    if (!prices.insert(parsed.price).second)
                    {
                        throw std::invalid_argument(
                            "book snapshot contains a duplicate price");
                    }
                    output.push_back(parsed);
                }
            };
            parse_side(message["bids"], event.bids);
            parse_side(message["asks"], event.asks);
        }

        bool supported_tick_size(const std::string &tick_size)
        {
            constexpr std::array<const char *, 6> supported{
                "0.1", "0.01", "0.005", "0.0025", "0.001", "0.0001"};
            return std::find(supported.begin(), supported.end(), tick_size) !=
                   supported.end();
        }

        void apply_change(std::vector<PriceLevel> &levels,
                          const PriceLevel &change,
                          bool bids)
        {
            const auto before = [bids](const PriceLevel &level, double price)
            {
                return bids ? level.price > price : level.price < price;
            };
            auto existing = std::lower_bound(levels.begin(), levels.end(),
                                             change.price, before);
            const bool same_price = existing != levels.end() &&
                                    std::abs(existing->price - change.price) < 1e-12;
            if (change.size <= 0.0)
            {
                if (same_price)
                {
                    levels.erase(existing);
                }
                return;
            }
            if (same_price)
            {
                *existing = change;
            }
            else
            {
                levels.insert(existing, change);
            }
        }
    }

    std::vector<MarketBookEvent> parse_market_book_events(const std::string &message)
    {
        const auto parsed = json::parse(message);
        const auto messages = parsed.is_array() ? parsed : json::array({parsed});
        std::vector<MarketBookEvent> events;

        for (const auto &item : messages)
        {
            const auto event_type = item.value("event_type", "");
            if (event_type == "book")
            {
                if (!item.is_object() || !item.contains("asset_id") ||
                    !item["asset_id"].is_string())
                {
                    throw std::invalid_argument(
                        "book snapshot requires a string asset_id");
                }
                MarketBookEvent event;
                event.asset_id = item["asset_id"].get<std::string>();
                event.snapshot = true;
                if (event.asset_id.empty())
                {
                    throw std::invalid_argument(
                        "book snapshot asset_id must not be empty");
                }
                parse_snapshot(item, event);
                events.push_back(std::move(event));
            }
            else if (event_type == "price_change" && item.contains("price_changes"))
            {
                for (const auto &change : item["price_changes"])
                {
                    const auto asset_id = change.value("asset_id", "");
                    if (asset_id.empty())
                    {
                        continue;
                    }
                    auto event = std::find_if(events.begin(), events.end(), [&asset_id](const auto &candidate)
                                              { return candidate.asset_id == asset_id; });
                    if (event == events.end())
                    {
                        events.push_back({asset_id});
                        event = std::prev(events.end());
                    }
                    const auto side = change.value("side", "");
                    if (side != "BUY" && side != "SELL")
                    {
                        throw std::invalid_argument(
                            "price change side must be BUY or SELL");
                    }
                    event->changes.push_back({side == "BUY", parse_level(change)});
                }
            }
        }
        return events;
    }

    void apply_market_book_event(Orderbook &book,
                                 const MarketBookEvent &event,
                                 uint64_t received_ns)
    {
        book.asset_id = event.asset_id;
        if (event.snapshot)
        {
            for (const auto &level : event.bids) validate_level(level);
            for (const auto &level : event.asks) validate_level(level);
            book.bids = event.bids;
            book.asks = event.asks;
            std::sort(book.bids.begin(), book.bids.end(), [](const auto &left, const auto &right)
                      { return left.price > right.price; });
            std::sort(book.asks.begin(), book.asks.end(), [](const auto &left, const auto &right)
                      { return left.price < right.price; });
        }
        else
        {
            for (const auto &change : event.changes)
            {
                validate_level(change.level);
                apply_change(change.bid ? book.bids : book.asks,
                             change.level, change.bid);
            }
        }
        book.timestamp_ns = received_ns;
    }

    std::optional<MarketTickSizeChange> parse_market_tick_size_change(const std::string &message)
    {
        const auto parsed = json::parse(message);
        if (!parsed.is_object() || parsed.value("event_type", "") != "tick_size_change")
        {
            return std::nullopt;
        }
        if (!parsed.contains("asset_id") || !parsed["asset_id"].is_string() ||
            !parsed.contains("new_tick_size") ||
            !parsed["new_tick_size"].is_string())
        {
            return std::nullopt;
        }
        MarketTickSizeChange change{parsed["asset_id"].get<std::string>(),
                                    parsed["new_tick_size"].get<std::string>()};
        if (change.asset_id.empty() ||
            !supported_tick_size(change.new_tick_size))
        {
            return std::nullopt;
        }
        return change;
    }
}
