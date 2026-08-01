#include "market_fetcher.hpp"
#include "bounded_workers.hpp"
#include "market_time.hpp"

#include <algorithm>
#include <ctime>
#include <iterator>
#include <memory>

namespace polymarket
{
    std::vector<uint64_t> MarketFetcher::get_15m_timestamps(int count)
    {
        return detail::aligned_market_timestamps(now_sec(), 15 * 60, count);
    }

    std::vector<uint64_t> MarketFetcher::get_4h_timestamps(int count)
    {
        return detail::aligned_market_timestamps(now_sec(), 4 * 60 * 60, count);
    }

    std::vector<std::string> MarketFetcher::generate_1h_slugs(int count)
    {
        static const std::vector<std::pair<std::string, std::string>> names = {
            {"btc", "bitcoin"}, {"eth", "ethereum"}, {"xrp", "xrp"},
            {"sol", "solana"}};
        std::vector<std::string> slugs;
        const auto now = static_cast<int64_t>(std::time(nullptr));
        for (const auto &[ticker, name] : names)
        {
            if (std::find(config_.crypto_tickers.begin(), config_.crypto_tickers.end(),
                          ticker) == config_.crypto_tickers.end())
            {
                continue;
            }
            auto ticker_slugs = detail::generate_new_york_hour_slugs(
                name, static_cast<uint64_t>(now), count);
            slugs.insert(slugs.end(),
                         std::make_move_iterator(ticker_slugs.begin()),
                         std::make_move_iterator(ticker_slugs.end()));
        }
        return slugs;
    }

    std::vector<MarketState> MarketFetcher::fetch_gamma_markets(
        const std::vector<std::pair<std::string, std::string>> &requests)
    {
        if (requests.empty())
        {
            return {};
        }
        constexpr std::size_t max_workers = 8;
        const auto worker_count = std::min(max_workers, requests.size());
        std::vector<std::unique_ptr<HttpClient>> clients;
        clients.reserve(worker_count);
        for (std::size_t index = 0; index < worker_count; ++index)
        {
            auto client = std::make_unique<HttpClient>();
            client->set_base_url(config_.gamma_api_url);
            client->set_timeout_ms(config_.http_timeout_ms);
            clients.push_back(std::move(client));
        }

        std::vector<std::optional<MarketState>> results(requests.size());
        detail::run_bounded_tasks(requests.size(), worker_count,
            [&](std::size_t index, std::size_t worker)
            {
                const auto &[slug, ticker] = requests[index];
                const auto response = clients[worker]->get("/events?slug=" + slug);
                if (response.ok())
                {
                    results[index] = parse_gamma_event(response.body, ticker);
                }
            });

        std::vector<MarketState> markets;
        for (auto &result : results)
        {
            if (result)
            {
                markets.push_back(std::move(*result));
            }
        }
        return markets;
    }

    std::vector<MarketState> MarketFetcher::fetch_timestamp_markets(
        const std::string &timeframe,
        const std::vector<uint64_t> &timestamps)
    {
        std::vector<std::pair<std::string, std::string>> requests;
        for (const auto &ticker : config_.crypto_tickers)
        {
            for (const auto timestamp : timestamps)
            {
                requests.emplace_back(ticker + "-updown-" + timeframe + "-" +
                                          std::to_string(timestamp),
                                      ticker);
            }
        }
        return fetch_gamma_markets(requests);
    }

    std::vector<MarketState> MarketFetcher::fetch_crypto_15m_markets()
    {
        return fetch_timestamp_markets("15m", get_15m_timestamps(3));
    }

    std::vector<MarketState> MarketFetcher::fetch_crypto_4h_markets()
    {
        return fetch_timestamp_markets("4h", get_4h_timestamps(3));
    }

    std::vector<MarketState> MarketFetcher::fetch_crypto_1h_markets()
    {
        std::vector<std::pair<std::string, std::string>> requests;
        for (auto &slug : generate_1h_slugs(3))
        {
            const auto ticker = detail::ticker_from_hour_slug(slug);
            if (!ticker.empty())
            {
                requests.emplace_back(std::move(slug), ticker);
            }
        }
        return fetch_gamma_markets(requests);
    }

    std::optional<MarketState> MarketFetcher::parse_gamma_event(
        const std::string &json_text, const std::string &ticker)
    {
        try
        {
            return detail::parse_gamma_market_json(json_text, ticker);
        }
        catch (const std::exception &)
        {
            return std::nullopt;
        }
    }
}
