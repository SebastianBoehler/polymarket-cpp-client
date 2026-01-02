#include "market_fetcher.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <ctime>

using json = nlohmann::json;

namespace polymarket
{

    MarketFetcher::MarketFetcher(const Config &config)
        : config_(config)
    {
        http_.set_base_url(config_.clob_rest_url);
        http_.set_timeout_ms(config_.http_timeout_ms);
    }

    std::vector<ClobMarket> MarketFetcher::fetch_all_markets(int max_markets)
    {
        std::vector<ClobMarket> markets;
        std::string next_cursor;

        while (markets.size() < static_cast<size_t>(max_markets))
        {
            std::string path = "/markets";
            if (!next_cursor.empty())
            {
                path += "?next_cursor=" + next_cursor;
            }

            auto response = http_.get(path);
            if (!response.ok())
            {
                std::cerr << "Failed to fetch markets: " << response.status_code
                          << " - " << response.error << std::endl;
                break;
            }

            try
            {
                auto parsed = parse_markets_response(response.body);
                if (parsed.empty())
                    break;

                for (auto &m : parsed)
                {
                    if (markets.size() >= static_cast<size_t>(max_markets))
                        break;
                    markets.push_back(std::move(m));
                }

                // Get next cursor from response
                auto j = json::parse(response.body);
                if (j.contains("next_cursor") && !j["next_cursor"].is_null())
                {
                    next_cursor = j["next_cursor"].get<std::string>();
                }
                else
                {
                    break;
                }
            }
            catch (const std::exception &e)
            {
                std::cerr << "JSON parse error: " << e.what() << std::endl;
                break;
            }
        }

        return markets;
    }

    std::vector<ClobMarket> MarketFetcher::fetch_neg_risk_markets(int max_markets)
    {
        auto all_markets = fetch_all_markets(max_markets * 5); // Fetch more to filter

        std::vector<ClobMarket> valid_markets;
        for (auto &m : all_markets)
        {
            // Filter for markets with valid tokens (Yes/No outcomes with token IDs)
            bool has_valid_tokens = m.tokens.size() == 2 &&
                                    !m.token_yes().empty() &&
                                    !m.token_no().empty();

            if (has_valid_tokens && !m.condition_id.empty())
            {
                valid_markets.push_back(std::move(m));
                if (valid_markets.size() >= static_cast<size_t>(max_markets))
                {
                    break;
                }
            }
        }

        std::cout << "[MarketFetcher] Found " << valid_markets.size()
                  << " markets with valid tokens" << std::endl;
        return valid_markets;
    }

    std::optional<ClobMarket> MarketFetcher::fetch_market(const std::string &condition_id)
    {
        auto response = http_.get("/markets/" + condition_id);
        if (!response.ok())
        {
            return std::nullopt;
        }

        auto markets = parse_markets_response("[" + response.body + "]");
        if (markets.empty())
        {
            return std::nullopt;
        }

        return markets[0];
    }

    std::optional<Orderbook> MarketFetcher::fetch_orderbook(const std::string &token_id)
    {
        auto response = http_.get("/book?token_id=" + token_id);
        if (!response.ok())
        {
            return std::nullopt;
        }

        return parse_orderbook_response(response.body);
    }

    std::vector<ClobMarket> MarketFetcher::parse_markets_response(const std::string &json_str)
    {
        std::vector<ClobMarket> markets;

        try
        {
            auto j = json::parse(json_str);

            // Handle both array and object with "data" field
            json market_array;
            if (j.is_array())
            {
                market_array = j;
            }
            else if (j.contains("data") && j["data"].is_array())
            {
                market_array = j["data"];
            }
            else
            {
                return markets;
            }

            for (const auto &item : market_array)
            {
                ClobMarket market;

                if (item.contains("condition_id"))
                {
                    market.condition_id = item["condition_id"].get<std::string>();
                }
                if (item.contains("question") && !item["question"].is_null())
                {
                    market.question = item["question"].get<std::string>();
                }
                if (item.contains("market_slug") && !item["market_slug"].is_null())
                {
                    market.market_slug = item["market_slug"].get<std::string>();
                }
                if (item.contains("neg_risk"))
                {
                    market.neg_risk = item["neg_risk"].get<bool>();
                }
                if (item.contains("active"))
                {
                    market.active = item["active"].get<bool>();
                }
                if (item.contains("closed"))
                {
                    market.closed = item["closed"].get<bool>();
                }

                if (item.contains("tokens") && item["tokens"].is_array())
                {
                    for (const auto &t : item["tokens"])
                    {
                        Token token;
                        if (t.contains("token_id"))
                        {
                            token.token_id = t["token_id"].get<std::string>();
                        }
                        if (t.contains("outcome"))
                        {
                            token.outcome = t["outcome"].get<std::string>();
                        }
                        market.tokens.push_back(token);
                    }
                }

                markets.push_back(std::move(market));
            }
        }
        catch (const std::exception &e)
        {
            std::cerr << "Parse error: " << e.what() << std::endl;
        }

        return markets;
    }

    std::optional<Orderbook> MarketFetcher::parse_orderbook_response(const std::string &json_str)
    {
        try
        {
            auto j = json::parse(json_str);

            Orderbook book;
            book.timestamp_ns = now_ns();

            if (j.contains("asset_id"))
            {
                book.asset_id = j["asset_id"].get<std::string>();
            }

            if (j.contains("bids") && j["bids"].is_array())
            {
                for (const auto &bid : j["bids"])
                {
                    PriceLevel level;
                    level.price = std::stod(bid["price"].get<std::string>());
                    level.size = std::stod(bid["size"].get<std::string>());
                    book.bids.push_back(level);
                }
            }

            if (j.contains("asks") && j["asks"].is_array())
            {
                for (const auto &ask : j["asks"])
                {
                    PriceLevel level;
                    level.price = std::stod(ask["price"].get<std::string>());
                    level.size = std::stod(ask["size"].get<std::string>());
                    book.asks.push_back(level);
                }
            }

            return book;
        }
        catch (const std::exception &e)
        {
            std::cerr << "Orderbook parse error: " << e.what() << std::endl;
            return std::nullopt;
        }
    }

    std::vector<uint64_t> MarketFetcher::get_15m_timestamps(int count)
    {
        std::vector<uint64_t> timestamps;
        uint64_t now = now_sec();
        uint64_t interval = 15 * 60;
        uint64_t current_window = (now / interval) * interval;

        for (int i = 0; i < count; i++)
        {
            timestamps.push_back(current_window + interval * i);
        }

        return timestamps;
    }

    std::vector<uint64_t> MarketFetcher::get_4h_timestamps(int count)
    {
        std::vector<uint64_t> timestamps;
        uint64_t now = now_sec();
        uint64_t interval = 4 * 60 * 60;
        uint64_t offset = 1 * 60 * 60; // 4h markets offset by 1h
        uint64_t adjusted = now - offset;
        uint64_t current_window = (adjusted / interval) * interval + offset;

        for (int i = -1; i < count; i++)
        {
            timestamps.push_back(current_window + interval * i);
        }

        return timestamps;
    }

    std::vector<std::string> MarketFetcher::generate_1h_slugs(int count)
    {
        std::vector<std::string> slugs;

        static const std::vector<std::string> months = {
            "january", "february", "march", "april", "may", "june",
            "july", "august", "september", "october", "november", "december"};

        static const std::vector<std::pair<std::string, std::string>> crypto_names = {
            {"btc", "bitcoin"},
            {"eth", "ethereum"},
            {"xrp", "xrp"},
            {"sol", "solana"}};

        // Get current time in ET (UTC-5)
        time_t now = time(nullptr);
        struct tm *utc = gmtime(&now);
        time_t et_time = now - 5 * 3600; // UTC-5
        struct tm *et = gmtime(&et_time);

        for (const auto &[ticker, name] : crypto_names)
        {
            for (int i = -1; i < count; i++)
            {
                time_t target_time = et_time + i * 3600;
                struct tm *target = gmtime(&target_time);

                int hour = target->tm_hour;
                std::string hour_str;
                if (hour == 0)
                    hour_str = "12am";
                else if (hour == 12)
                    hour_str = "12pm";
                else if (hour < 12)
                    hour_str = std::to_string(hour) + "am";
                else
                    hour_str = std::to_string(hour - 12) + "pm";

                std::string slug = name + "-up-or-down-" +
                                   months[target->tm_mon] + "-" +
                                   std::to_string(target->tm_mday) + "-" +
                                   hour_str + "-et";
                slugs.push_back(slug);
            }
        }

        return slugs;
    }

    std::vector<MarketState> MarketFetcher::fetch_crypto_15m_markets()
    {
        std::vector<MarketState> markets;
        auto timestamps = get_15m_timestamps(3);

        HttpClient gamma_http;
        gamma_http.set_base_url(config_.gamma_api_url);
        gamma_http.set_timeout_ms(config_.http_timeout_ms);

        std::cout << "Fetching crypto up/down 15m markets from Gamma API...\n"
                  << std::endl;

        for (const auto &ticker : config_.crypto_tickers)
        {
            for (const auto &ts : timestamps)
            {
                std::string slug = ticker + "-updown-15m-" + std::to_string(ts);
                std::string path = "/events?slug=" + slug;

                auto response = gamma_http.get(path);
                if (!response.ok())
                    continue;

                auto market = parse_gamma_event(response.body, ticker);
                if (market)
                {
                    std::cout << "  Found: " << ticker << " - " << market->slug << std::endl;
                    markets.push_back(std::move(*market));
                }
            }
        }

        std::cout << "\nFound " << markets.size() << " crypto 15m markets\n"
                  << std::endl;
        return markets;
    }

    std::vector<MarketState> MarketFetcher::fetch_crypto_4h_markets()
    {
        std::vector<MarketState> markets;
        auto timestamps = get_4h_timestamps(3);

        HttpClient gamma_http;
        gamma_http.set_base_url(config_.gamma_api_url);
        gamma_http.set_timeout_ms(config_.http_timeout_ms);

        std::cout << "Fetching crypto up/down 4h markets from Gamma API...\n"
                  << std::endl;

        for (const auto &ticker : config_.crypto_tickers)
        {
            for (const auto &ts : timestamps)
            {
                std::string slug = ticker + "-updown-4h-" + std::to_string(ts);
                std::string path = "/events?slug=" + slug;

                auto response = gamma_http.get(path);
                if (!response.ok())
                    continue;

                auto market = parse_gamma_event(response.body, ticker);
                if (market)
                {
                    std::cout << "  Found: " << ticker << " - " << market->slug << std::endl;
                    markets.push_back(std::move(*market));
                }
            }
        }

        std::cout << "\nFound " << markets.size() << " crypto 4h markets\n"
                  << std::endl;
        return markets;
    }

    std::vector<MarketState> MarketFetcher::fetch_crypto_1h_markets()
    {
        std::vector<MarketState> markets;
        auto slugs = generate_1h_slugs(3);

        HttpClient gamma_http;
        gamma_http.set_base_url(config_.gamma_api_url);
        gamma_http.set_timeout_ms(config_.http_timeout_ms);

        std::cout << "Fetching crypto up/down 1h markets from Gamma API...\n"
                  << std::endl;

        for (const auto &slug : slugs)
        {
            std::string path = "/events?slug=" + slug;

            auto response = gamma_http.get(path);
            if (!response.ok())
                continue;

            // Extract ticker from slug
            std::string ticker = slug.substr(0, slug.find('-'));

            auto market = parse_gamma_event(response.body, ticker);
            if (market)
            {
                markets.push_back(std::move(*market));
                std::cout << "  Found: " << slug << std::endl;
            }
        }

        std::cout << "\nFound " << markets.size() << " crypto 1h markets\n"
                  << std::endl;
        return markets;
    }

    std::optional<MarketState> MarketFetcher::parse_gamma_event(const std::string &json_str, const std::string &ticker)
    {
        try
        {
            auto j = json::parse(json_str);

            if (!j.is_array() || j.empty())
            {
                return std::nullopt;
            }

            const auto &event = j[0];
            if (!event.contains("markets") || !event["markets"].is_array() || event["markets"].empty())
            {
                return std::nullopt;
            }

            const auto &m = event["markets"][0];

            if (!m.contains("clobTokenIds"))
            {
                return std::nullopt;
            }

            auto token_ids = json::parse(m["clobTokenIds"].get<std::string>());
            if (!token_ids.is_array() || token_ids.size() < 2)
            {
                return std::nullopt;
            }

            MarketState state;
            state.slug = m.contains("slug") ? m["slug"].get<std::string>() : event["slug"].get<std::string>();
            state.title = m.contains("question") ? m["question"].get<std::string>() : ticker;
            state.symbol = ticker;
            state.condition_id = m["conditionId"].get<std::string>();
            state.token_yes = token_ids[0].get<std::string>();
            state.token_no = token_ids[1].get<std::string>();

            return state;
        }
        catch (const std::exception &e)
        {
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

        // Extract symbol from slug
        auto pos = state.slug.find('-');
        state.symbol = pos != std::string::npos ? state.slug.substr(0, pos) : "unknown";

        return state;
    }

} // namespace polymarket
