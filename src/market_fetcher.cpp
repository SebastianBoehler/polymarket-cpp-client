#include "market_fetcher.hpp"
#include "query_encoding.hpp"
#include "rest_orderbook_parsing.hpp"
#include <nlohmann/json.hpp>

#include <iostream>
#include <unordered_set>

using json = nlohmann::json;

namespace polymarket
{
    MarketFetcher::MarketFetcher(const Config &config) : config_(config)
    {
        http_.set_base_url(config_.clob_rest_url);
        http_.set_timeout_ms(config_.http_timeout_ms);
    }

    std::vector<ClobMarket> MarketFetcher::fetch_all_markets(int max_markets)
    {
        if (max_markets <= 0)
        {
            return {};
        }
        return fetch_market_pages(static_cast<std::size_t>(max_markets), false);
    }

    std::vector<ClobMarket> MarketFetcher::fetch_market_pages(
        std::size_t limit, bool neg_risk_only)
    {
        constexpr std::size_t max_pages = 1000;
        const std::string endpoint = neg_risk_only ? "/sampling-markets" : "/markets";
        const uint64_t current_time_ms = now_sec() * 1000;
        std::vector<ClobMarket> markets;
        std::string next_cursor;
        std::unordered_set<std::string> seen_cursors;
        for (std::size_t page_number = 0;
             page_number < max_pages && markets.size() < limit;
             ++page_number)
        {
            if (!seen_cursors.insert(next_cursor).second)
                break;
            const std::string path = next_cursor.empty()
                                         ? endpoint
                                         : endpoint + "?next_cursor=" +
                                               detail::percent_encode_query_value(next_cursor);
            const auto response = http_.get(path);
            if (!response.ok())
            {
                std::cerr << "Failed to fetch markets: " << response.status_code
                          << " - " << response.error << '\n';
                break;
            }
            try
            {
                auto page = parse_markets_response(response.body);
                for (auto &market : page)
                {
                    const bool valid_neg_risk = market.neg_risk && market.active &&
                                                !market.closed && !market.condition_id.empty() &&
                                                !market.token_yes().empty() &&
                                                !market.token_no().empty() &&
                                                market.end_time_ms > current_time_ms;
                    if (neg_risk_only && !valid_neg_risk)
                    {
                        continue;
                    }
                    if (markets.size() == limit)
                    {
                        break;
                    }
                    markets.push_back(std::move(market));
                }
                const auto parsed = json::parse(response.body);
                if (!parsed.is_object() || !parsed.contains("next_cursor") ||
                    parsed["next_cursor"].is_null())
                {
                    break;
                }
                const auto cursor = parsed["next_cursor"].get<std::string>();
                if (cursor.empty() || cursor == next_cursor || cursor == "LTE=")
                {
                    break;
                }
                next_cursor = cursor;
            }
            catch (const std::exception &error)
            {
                std::cerr << "Market response parse error: " << error.what() << '\n';
                break;
            }
        }
        return markets;
    }

    std::vector<ClobMarket> MarketFetcher::fetch_neg_risk_markets(int max_markets)
    {
        if (max_markets <= 0)
        {
            return {};
        }
        return fetch_market_pages(static_cast<std::size_t>(max_markets), true);
    }

    std::optional<ClobMarket> MarketFetcher::fetch_market(
        const std::string &condition_id)
    {
        if (condition_id.empty()) return std::nullopt;
        const auto response = http_.get("/markets/" + condition_id);
        if (!response.ok())
        {
            return std::nullopt;
        }
        try
        {
            auto markets = parse_markets_response("[" + response.body + "]");
            return markets.size() == 1 &&
                           markets.front().condition_id == condition_id
                       ? std::optional<ClobMarket>(std::move(markets.front()))
                       : std::nullopt;
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    std::optional<Orderbook> MarketFetcher::fetch_orderbook(const std::string &token_id)
    {
        const auto response = http_.get(
            "/book?token_id=" + detail::percent_encode_query_value(token_id));
        return response.ok()
                   ? parse_orderbook_response(response.body, token_id)
                   : std::nullopt;
    }

    bool MarketFetcher::refresh_market_metadata(MarketState &market)
    {
        const auto response = http_.get("/clob-markets/" + market.condition_id);
        if (!response.ok())
        {
            return false;
        }
        try
        {
            return detail::apply_clob_market_info_json(market, response.body);
        }
        catch (const std::exception &error)
        {
            std::cerr << "CLOB market info parse error: " << error.what() << '\n';
            return false;
        }
    }

    std::vector<ClobMarket> MarketFetcher::parse_markets_response(
        const std::string &json_text)
    {
        return detail::parse_clob_markets_json(json_text);
    }

    std::optional<Orderbook> MarketFetcher::parse_orderbook_response(
        const std::string &json_text, const std::string &expected_asset_id)
    {
        try
        {
            return detail::parse_rest_orderbook_json(json_text,
                                                     expected_asset_id);
        }
        catch (const std::exception &error)
        {
            std::cerr << "Orderbook parse error: " << error.what() << '\n';
            return std::nullopt;
        }
    }

    MarketState MarketFetcher::to_market_state(const ClobMarket &market)
    {
        MarketState state;
        state.slug = market.market_slug.empty() ? market.condition_id : market.market_slug;
        state.title = market.question.empty() ? market.market_slug : market.question;
        state.condition_id = market.condition_id;
        state.token_yes = market.token_yes();
        state.token_no = market.token_no();
        state.minimum_order_size = market.minimum_order_size;
        state.minimum_tick_size = market.minimum_tick_size;
        state.end_time_ms = market.end_time_ms;
        state.neg_risk = market.neg_risk;
        state.fees_enabled = market.fees_enabled;
        state.fee_rate = market.fee_rate;
        state.fee_exponent = market.fee_exponent;
        const auto separator = state.slug.find('-');
        state.symbol = separator == std::string::npos
                           ? "unknown"
                           : state.slug.substr(0, separator);
        return state;
    }
}
