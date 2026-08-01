#include "market_fetcher.hpp"
#include "market_fee_parsing.hpp"
#include "market_time.hpp"
#include "rest_numeric.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <stdexcept>

using json = nlohmann::json;

namespace polymarket
{
    namespace
    {
        std::string json_decimal(const json &value)
        {
            return value.is_string() ? value.get<std::string>() : value.dump();
        }

        json json_array_field(const json &value)
        {
            return value.is_string() ? json::parse(value.get<std::string>()) : value;
        }

        std::string lowercase(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch)
                           { return static_cast<char>(std::tolower(ch)); });
            return value;
        }

    }

    namespace detail
    {
        std::vector<ClobMarket> filter_neg_risk_markets(
            const std::vector<ClobMarket> &markets, std::size_t limit)
        {
            std::vector<ClobMarket> filtered;
            for (const auto &market : markets)
            {
                if (!market.neg_risk || !market.active || market.closed ||
                    market.condition_id.empty() || market.token_yes().empty() ||
                    market.token_no().empty())
                {
                    continue;
                }
                filtered.push_back(market);
                if (filtered.size() == limit)
                {
                    break;
                }
            }
            return filtered;
        }

        std::optional<MarketState> parse_gamma_market_json(const std::string &json_text,
                                                           const std::string &ticker)
        {
            const auto events = json::parse(json_text);
            if (!events.is_array() || events.empty())
            {
                return std::nullopt;
            }
            const auto &event = events[0];
            if (!event.value("active", false) || event.value("closed", false) ||
                !event.contains("markets") || !event["markets"].is_array())
            {
                return std::nullopt;
            }
            for (const auto &market : event["markets"])
            {
                if (!market.value("active", false) || market.value("closed", false) ||
                    !market.value("acceptingOrders", false) ||
                    !market.contains("clobTokenIds") || !market.contains("outcomes"))
                {
                    continue;
                }
                const auto tokens = json_array_field(market["clobTokenIds"]);
                const auto outcomes = json_array_field(market["outcomes"]);
                if (!tokens.is_array() || tokens.size() != outcomes.size())
                {
                    continue;
                }
                MarketState state;
                state.slug = market.value("slug", event.value("slug", ""));
                state.title = market.value("question", ticker);
                state.symbol = ticker;
                state.condition_id = market.value("conditionId", "");
                for (std::size_t index = 0; index < outcomes.size(); ++index)
                {
                    const auto outcome = lowercase(outcomes[index].get<std::string>());
                    if (outcome == "yes" || outcome == "up")
                    {
                        state.token_yes = tokens[index].get<std::string>();
                    }
                    else if (outcome == "no" || outcome == "down")
                    {
                        state.token_no = tokens[index].get<std::string>();
                    }
                }
                if (state.condition_id.empty() || state.token_yes.empty() ||
                    state.token_no.empty())
                {
                    continue;
                }
                state.minimum_order_size = market.value("orderMinSize", 0.0);
                if (market.contains("orderPriceMinTickSize"))
                {
                    (void)detail::json_orderbook_price(
                        market["orderPriceMinTickSize"]);
                    state.minimum_tick_size =
                        json_decimal(market["orderPriceMinTickSize"]);
                }
                state.end_time_ms = parse_iso8601_ms(
                    market.value("endDate", event.value("endDate", "")));
                state.neg_risk = market.value("negRisk", event.value("negRisk", false));
                const auto fees = parse_market_fee_fields(market);
                state.fees_enabled = fees.enabled;
                state.fee_rate = fees.curve_rate;
                state.fee_exponent = fees.curve_exponent;
                return state;
            }
            return std::nullopt;
        }

        bool apply_clob_market_info_json(MarketState &market,
                                         const std::string &json_text)
        {
            const auto info = json::parse(json_text);
            if (!info.is_object() || !info.contains("mos") || !info.contains("mts"))
            {
                return false;
            }
            market.minimum_order_size = detail::strict_json_number(info["mos"]);
            (void)detail::json_orderbook_price(info["mts"]);
            market.minimum_tick_size = json_decimal(info["mts"]);
            market.fees_enabled = false;
            market.fee_rate = 0.0;
            market.fee_exponent = 1;
            if (info.contains("fd") && info["fd"].is_object())
            {
                const auto &fee = info["fd"];
                market.fee_rate = fee.value("r", 0.0);
                market.fee_exponent = fee.value("e", 1);
                market.fees_enabled = market.fee_rate > 0.0;
            }
            return !market.minimum_tick_size.empty() && market.minimum_order_size > 0.0;
        }
    }
}
