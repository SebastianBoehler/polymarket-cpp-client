#include "market_fetcher.hpp"
#include "bounded_workers.hpp"
#include "clob_client_test_fixture.hpp"
#include "market_time.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <future>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <vector>

using namespace polymarket;

namespace
{
    bool check(bool value, const char *name)
    {
        if (!value)
        {
            std::cerr << "failed: " << name << '\n';
        }
        return value;
    }

    bool test_opaque_query_encoding()
    {
        clob_test::LocalServer server;
        server.enqueue(R"({"next_cursor":"N+/=","data":[{"condition_id":"first","tokens":[{"token_id":"a","outcome":"Yes"}]}]})");
        server.enqueue(R"({"next_cursor":"LTE=","data":[{"condition_id":"second","tokens":[{"token_id":"b","outcome":"No"}]}]})");
        server.enqueue(R"({"asset_id":"tok+/=","bids":[],"asks":[]})");
        Config config;
        config.clob_rest_url = server.url();
        MarketFetcher fetcher(config);

        const auto markets = fetcher.fetch_all_markets(2);
        const auto book = fetcher.fetch_orderbook("tok+/=");
        const auto requests = server.requests();
        return check(markets.size() == 2, "cursor pages are accumulated") &&
               check(book.has_value(), "encoded token orderbook is parsed") &&
               check(requests.size() == 3, "market fetcher request count") &&
               check(requests[0].target == "/markets", "initial market request") &&
               check(requests[1].target == "/markets?next_cursor=N%2B%2F%3D",
                     "opaque market cursor is percent encoded") &&
               check(requests[2].target == "/book?token_id=tok%2B%2F%3D",
                     "orderbook token ID is percent encoded");
    }

    bool test_neg_risk_paginates_until_matches()
    {
        clob_test::LocalServer server;
        server.enqueue(R"({"next_cursor":"next+/=","data":[{"condition_id":"expired","neg_risk":true,"active":true,"closed":false,"end_date_iso":"2025-01-01T00:00:00Z","tokens":[{"token_id":"expired-yes","outcome":"Yes"},{"token_id":"expired-no","outcome":"No"}]}]})");
        server.enqueue(R"({"next_cursor":"LTE=","data":[{"condition_id":"current","neg_risk":true,"active":true,"closed":false,"end_date_iso":"2099-01-01T00:00:00Z","tokens":[{"token_id":"yes","outcome":"Yes"},{"token_id":"no","outcome":"No"}]}]})");
        Config config;
        config.clob_rest_url = server.url();
        MarketFetcher fetcher(config);

        const auto markets = fetcher.fetch_neg_risk_markets(1);
        const auto requests = server.requests();
        return check(markets.size() == 1 && markets[0].condition_id == "current",
                     "neg-risk discovery scans past expired active markets") &&
               check(requests.size() == 2, "neg-risk discovery follows the cursor") &&
               check(requests[0].target == "/sampling-markets",
                     "neg-risk discovery uses the current sampling feed") &&
               check(requests[1].target == "/sampling-markets?next_cursor=next%2B%2F%3D",
                     "neg-risk cursor is encoded");
    }

    bool test_market_cursor_cycle_stops_before_repeat()
    {
        clob_test::LocalServer server;
        server.enqueue(R"({"next_cursor":"A","data":[{"condition_id":"one","tokens":[{"token_id":"1","outcome":"Yes"}]}]})");
        server.enqueue(R"({"next_cursor":"B","data":[{"condition_id":"two","tokens":[{"token_id":"2","outcome":"Yes"}]}]})");
        server.enqueue(R"({"next_cursor":"A","data":[{"condition_id":"three","tokens":[{"token_id":"3","outcome":"Yes"}]}]})");
        Config config;
        config.clob_rest_url = server.url();
        MarketFetcher fetcher(config);

        const auto markets = fetcher.fetch_all_markets(10);
        const auto requests = server.requests();
        return check(markets.size() == 3, "market pages before a cursor cycle are retained") &&
               check(requests.size() == 3, "market cursor cycle stops before a repeated request");
    }
}

int main()
{
    if (!test_opaque_query_encoding())
        return 1;
    if (!test_neg_risk_paginates_until_matches())
        return 1;
    if (!test_market_cursor_cycle_stops_before_repeat())
        return 1;

    const auto clob = detail::parse_clob_markets_json(R"({"data":[
        {"condition_id":"neg","question":"Neg","market_slug":"neg-market","neg_risk":true,"active":true,"closed":false,"minimum_order_size":5,"minimum_tick_size":0.001,"end_date_iso":"2026-08-01T05:30:00Z","fees_enabled":true,"fee_schedule":{"rate":0.04,"exponent":1},"tokens":[{"token_id":"yes-neg","outcome":"Yes"},{"token_id":"no-neg","outcome":"No"}]},
        {"condition_id":"regular","neg_risk":false,"active":true,"closed":false,"tokens":[{"token_id":"yes-reg","outcome":"Yes"},{"token_id":"no-reg","outcome":"No"}]},
        {"condition_id":"missing-bools","tokens":[{"token_id":"other","outcome":"Other"}]}
    ]})");
    if (!check(clob.size() == 3, "CLOB fixture parsed") ||
        !check(!clob[2].neg_risk && !clob[2].active && !clob[2].closed,
               "missing market bools are initialized false") ||
        !check(clob[0].minimum_tick_size == "0.001", "CLOB tick metadata") ||
        !check(clob[0].end_time_ms == 1'785'562'200'000ULL, "CLOB expiry metadata") ||
        !check(std::abs(clob[0].fee_rate - 0.04) < 1e-12, "CLOB fee metadata"))
    {
        return 1;
    }

    const auto mixed = detail::parse_clob_markets_json(
        R"({"data":[{"condition_id":"valid","tokens":[{"token_id":"yes","outcome":"Yes"}]},{},{"condition_id":"missing-tokens"},{"condition_id":"bad-token","tokens":[{"token_id":"","outcome":"Yes"}]}]})");
    if (!check(mixed.size() == 1 && mixed[0].condition_id == "valid",
               "malformed CLOB market records are never materialized"))
        return 1;

    const auto filtered = detail::filter_neg_risk_markets(clob, 10);
    if (!check(filtered.size() == 1, "neg-risk filter excludes ordinary markets") ||
        !check(filtered[0].condition_id == "neg", "neg-risk filter keeps matching market"))
    {
        return 1;
    }

    const auto gamma = detail::parse_gamma_market_json(R"([{
      "slug":"btc-updown-15m-1785561300","active":true,"closed":false,
      "markets":[{"question":"Bitcoin Up or Down","conditionId":"gamma-condition",
        "slug":"btc-updown-15m-1785561300","endDate":"2026-08-01T05:30:00Z",
        "outcomes":"[\"Up\",\"Down\"]","clobTokenIds":"[\"up-token\",\"down-token\"]",
        "active":true,"closed":false,"acceptingOrders":true,"orderPriceMinTickSize":0.01,
        "orderMinSize":5,"negRisk":false,"feesEnabled":true,
        "feeSchedule":{"exponent":1,"rate":0.07,"takerOnly":true}}]}])", "btc");
    if (!check(gamma.has_value(), "Gamma fixture parsed") ||
        !check(gamma->token_yes == "up-token" && gamma->token_no == "down-token",
               "outcome labels determine complementary tokens") ||
        !check(gamma->minimum_tick_size == "0.01", "Gamma tick is carried") ||
        !check(gamma->minimum_order_size == 5.0, "Gamma minimum size is carried") ||
        !check(!gamma->neg_risk, "Gamma neg-risk is not guessed") ||
        !check(gamma->fees_enabled && std::abs(gamma->fee_rate - 0.07) < 1e-12,
               "Gamma fee schedule is carried") ||
        !check(gamma->end_time_ms == 1'785'562'200'000ULL, "Gamma expiry is carried"))
    {
        return 1;
    }

    auto refreshed = *gamma;
    if (!check(detail::apply_clob_market_info_json(
                   refreshed,
                   R"({"mos":5,"mts":0.001,"fd":{"r":0.07,"e":1,"to":true}})"),
               "CLOB market info parsed") ||
        !check(refreshed.minimum_tick_size == "0.001", "live tick replaces Gamma snapshot") ||
        !check(refreshed.fees_enabled && std::abs(refreshed.fee_rate - 0.07) < 1e-12,
               "live fee curve replaces metadata snapshot"))
    {
        return 1;
    }

    if (!check(detail::format_new_york_hour_slug("bitcoin", 1'772'951'400ULL) ==
                   "bitcoin-up-or-down-march-8-1am-et",
               "EST slug before spring transition") ||
        !check(detail::format_new_york_hour_slug("bitcoin", 1'772'955'000ULL) ==
                   "bitcoin-up-or-down-march-8-3am-et",
               "EDT slug skips nonexistent 2am hour") ||
        !check(detail::format_new_york_hour_slug("bitcoin", 1'793'514'600ULL) ==
                   "bitcoin-up-or-down-november-1-1am-et",
               "EST slug after fall transition") ||
        !check(detail::ticker_from_hour_slug("bitcoin-up-or-down-march-8-1am-et") == "btc",
               "hour slug maps back to configured ticker") ||
        !check(detail::ticker_from_hour_slug("unknown-up-or-down-march-8-1am-et").empty(),
               "unknown hour slug is rejected"))
    {
        return 1;
    }

    const auto fall_back_slugs = detail::generate_new_york_hour_slugs(
        "bitcoin", 1'793'514'600ULL, 3);
    if (!check(fall_back_slugs.size() == 3, "1h discovery returns requested unique count") ||
        !check(std::adjacent_find(fall_back_slugs.begin(), fall_back_slugs.end()) ==
                   fall_back_slugs.end(),
               "1h discovery deduplicates repeated DST hour") ||
        !check(fall_back_slugs[0] == "bitcoin-up-or-down-november-1-1am-et" &&
                   fall_back_slugs[1] == "bitcoin-up-or-down-november-1-2am-et",
               "1h discovery advances past repeated DST hour"))
    {
        return 1;
    }

    if (!check(detail::parse_iso8601_ms("2024-02-29T23:59:59.1234Z") ==
                   1'709'251'199'123ULL,
               "valid leap-day timestamp parses") ||
        !check(detail::parse_iso8601_ms("1970-01-01T00:00:00.1Z") == 100,
               "fractional seconds are padded to milliseconds") ||
        !check(detail::parse_iso8601_ms("1969-12-31T23:59:59Z") == 0,
               "pre-epoch timestamp is rejected") ||
        !check(detail::parse_iso8601_ms("2025-02-29T00:00:00Z") == 0,
               "non-leap February 29 is rejected") ||
        !check(detail::parse_iso8601_ms("2026-13-01T00:00:00Z") == 0,
               "invalid month is rejected") ||
        !check(detail::parse_iso8601_ms("2026-04-31T00:00:00Z") == 0,
               "invalid month day is rejected") ||
        !check(detail::parse_iso8601_ms("2026-01-01T24:00:00Z") == 0,
               "invalid hour is rejected") ||
        !check(detail::parse_iso8601_ms("2026-01-01T00:60:00Z") == 0,
               "invalid minute is rejected") ||
        !check(detail::parse_iso8601_ms("2026-01-01T00:00:60Z") == 0,
               "invalid second is rejected") ||
        !check(detail::parse_iso8601_ms("2026-01-01T00:00:00.Z") == 0,
               "empty fraction is rejected") ||
        !check(detail::parse_iso8601_ms("2026-01-01T00:00:00Zjunk") == 0,
               "trailing timestamp data is rejected"))
    {
        return 1;
    }

    const auto four_hour = detail::aligned_market_timestamps(1'785'561'458ULL, 4 * 60 * 60, 2);
    if (!check(four_hour.size() == 2, "4h timestamp count") ||
        !check(four_hour[0] == 1'785'556'800ULL, "4h window aligns to UTC boundary") ||
        !check(four_hour[1] == 1'785'571'200ULL, "next 4h window"))
    {
        return 1;
    }

    std::mutex worker_mutex;
    std::condition_variable worker_cv;
    std::vector<int> visits(6, 0);
    int active = 0;
    int peak = 0;
    bool release = false;
    auto workers = std::async(std::launch::async, [&]
    {
        detail::run_bounded_tasks(6, 3, [&](std::size_t task, std::size_t)
        {
            std::unique_lock lock(worker_mutex);
            ++active;
            peak = std::max(peak, active);
            ++visits[task];
            worker_cv.notify_all();
            worker_cv.wait(lock, [&] { return release; });
            --active;
        });
    });
    {
        std::unique_lock lock(worker_mutex);
        if (!worker_cv.wait_for(lock, std::chrono::seconds(1), [&] { return active == 3; }))
        {
            release = true;
            lock.unlock();
            worker_cv.notify_all();
            workers.wait();
            std::cerr << "bounded discovery workers did not run concurrently\n";
            return 1;
        }
        release = true;
    }
    worker_cv.notify_all();
    workers.get();
    if (!check(peak == 3, "discovery concurrency is bounded") ||
        !check(std::all_of(visits.begin(), visits.end(), [](int count) { return count == 1; }),
               "each discovery request runs exactly once"))
    {
        return 1;
    }

    bool task_failure_propagated = false;
    try
    {
        detail::run_bounded_tasks(4, 2, [](std::size_t task, std::size_t)
        {
            if (task == 0)
            {
                throw std::runtime_error("worker failure");
            }
        });
    }
    catch (const std::runtime_error &)
    {
        task_failure_propagated = true;
    }
    if (!check(task_failure_propagated,
               "bounded worker failures propagate after all workers are joined"))
    {
        return 1;
    }

    return 0;
}
