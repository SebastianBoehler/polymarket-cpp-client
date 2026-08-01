#include "orderbook.hpp"
#include "websocket_test_server.hpp"

#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <limits>
#include <mutex>
#include <optional>
#include <thread>

using namespace polymarket;
using namespace std::chrono_literals;

namespace
{
    template <typename Predicate>
    bool wait_until(Predicate predicate, std::chrono::milliseconds timeout)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (predicate()) return true;
            std::this_thread::sleep_for(2ms);
        }
        return predicate();
    }

    Config local_config(const websocket_test::LocalWebSocketServer &server)
    {
        Config config;
        config.clob_ws_url = server.url();
        config.ws_connect_timeout_ms = 1'000;
        config.max_book_age_ms = 60'000;
        config.trigger_combined = 0.98;
        return config;
    }

    bool invalid_config_is_rejected()
    {
        const auto rejected = [](Config config)
        {
            try
            {
                OrderbookManager books(config);
            }
            catch (const std::invalid_argument &)
            {
                return true;
            }
            return false;
        };

        Config invalid_trigger;
        invalid_trigger.trigger_combined =
            std::numeric_limits<double>::quiet_NaN();
        Config excessive_trigger;
        excessive_trigger.trigger_combined = 1.01;
        Config invalid_ping;
        invalid_ping.ws_ping_interval_ms = 0;
        Config invalid_connect_timeout;
        invalid_connect_timeout.ws_connect_timeout_ms = 0;
        Config invalid_book_age;
        invalid_book_age.max_book_age_ms = 0;
        Config overflowing_book_age;
        overflowing_book_age.max_book_age_ms =
            std::numeric_limits<uint64_t>::max();
        return rejected(invalid_trigger) && rejected(excessive_trigger) &&
               rejected(invalid_ping) && rejected(invalid_connect_timeout) &&
               rejected(invalid_book_age) && rejected(overflowing_book_age);
    }

    MarketState market()
    {
        MarketState value;
        value.slug = "permit-race";
        value.condition_id = "condition";
        value.token_yes = "yes";
        value.token_no = "no";
        value.minimum_order_size = 1.0;
        value.minimum_tick_size = "0.01";
        return value;
    }

    bool paused_callback_rejects_gap()
    {
        websocket_test::LocalWebSocketServer server;
        OrderbookManager books(local_config(server));
        std::mutex mutex;
        std::optional<StreamGenerationPermit> observed;
        std::atomic<unsigned int> admissions{0};
        std::promise<void> entered;
        std::promise<void> resume;
        std::promise<void> done;
        auto resume_future = resume.get_future().share();
        books.on_arb_opportunity_with_permit(
            [&](const MarketState &, double, StreamGenerationPermit permit)
        {
            {
                std::lock_guard lock(mutex);
                observed = permit;
            }
            entered.set_value();
            resume_future.wait();
            if (books.is_stream_current(permit)) admissions.fetch_add(1);
            done.set_value();
        });
        books.subscribe(market());
        if (!books.connect()) return false;
        const bool sent = server.send_to_clients(
            R"([{"event_type":"book","asset_id":"yes","bids":[],"asks":[{"price":"0.45","size":"100"}]},{"event_type":"book","asset_id":"no","bids":[],"asks":[{"price":"0.48","size":"100"}]}])");
        auto entered_future = entered.get_future();
        if (!sent || entered_future.wait_for(1s) != std::future_status::ready)
        {
            resume.set_value();
            books.stop();
            return false;
        }
        StreamGenerationPermit permit;
        {
            std::lock_guard lock(mutex);
            permit = *observed;
        }
        server.close_clients();
        const bool invalidated = wait_until(
            [&] { return !books.is_stream_current(permit); }, 1s);
        resume.set_value();
        auto done_future = done.get_future();
        const bool completed = done_future.wait_for(1s) == std::future_status::ready;
        books.stop();
        return invalidated && completed && admissions.load() == 0;
    }

    bool malformed_side_forces_resnapshot()
    {
        websocket_test::LocalWebSocketServer server;
        OrderbookManager books(local_config(server));
        std::atomic<unsigned int> admissions{0};
        books.on_arb_opportunity_with_permit(
            [&](const MarketState &, double, StreamGenerationPermit)
            { admissions.fetch_add(1); });
        books.subscribe(market());
        if (!books.connect()) return false;
        bool ok = server.send_to_clients(
            R"([{"event_type":"book","asset_id":"yes","bids":[],"asks":[{"price":"0.60","size":"100"}]},{"event_type":"book","asset_id":"no","bids":[],"asks":[{"price":"0.60","size":"100"}]}])");
        ok = wait_until([&] { return books.total_updates() == 2; }, 1s) && ok;
        ok = server.send_to_clients(
                 R"({"event_type":"price_change","price_changes":[{"asset_id":"yes","price":"0.55","size":"100","side":"SELL"},{"asset_id":"yes","price":"0.10","size":"100","side":"HOLD"}]})") &&
             ok;
        ok = wait_until([&] { return !books.get_orderbook("yes").has_value(); }, 1s) && ok;
        books.stop();
        return ok && books.total_updates() == 2 && admissions.load() == 0;
    }

    bool fee_limited_budget_depth_is_admitted()
    {
        websocket_test::LocalWebSocketServer server;
        OrderbookManager books(local_config(server));
        auto value = market();
        value.minimum_order_size = 5.0;
        value.fees_enabled = true;
        value.fee_rate = 0.07;
        value.fee_exponent = 1;
        std::atomic<unsigned int> executable{0};
        books.on_arb_opportunity_with_permit(
            [&](const MarketState &snapshot, double, StreamGenerationPermit)
            {
                ArbSizingInput input{snapshot.best_ask_yes, snapshot.best_ask_no,
                                     snapshot.best_ask_yes_size, snapshot.best_ask_no_size,
                                     5.0, 0.01, 0.01, snapshot.minimum_order_size,
                                     snapshot.fee_rate, snapshot.fee_exponent};
                if (size_complementary_arb(input).executable) executable++;
            });
        books.subscribe(value);
        if (!books.connect()) return false;
        const bool sent = server.send_to_clients(
            R"([{"event_type":"book","asset_id":"yes","bids":[],"asks":[{"price":"0.45","size":"10"}]},{"event_type":"book","asset_id":"no","bids":[],"asks":[{"price":"0.48","size":"10"}]}])");
        const bool admitted = sent && wait_until([&] { return executable.load() == 1; }, 1s);
        books.stop();
        return admitted;
    }
}

int main()
{
    if (!invalid_config_is_rejected())
    {
        std::cerr << "invalid orderbook configuration was accepted\n";
        return 1;
    }
    if (!paused_callback_rejects_gap())
    {
        std::cerr << "stale stream permit admitted a paused arbitrage callback\n";
        return 1;
    }
    if (!malformed_side_forces_resnapshot())
    {
        std::cerr << "malformed price-change side mutated or retained the live book\n";
        return 1;
    }
    if (!fee_limited_budget_depth_is_admitted())
    {
        std::cerr << "fee-inclusive budget sizing was suppressed by a raw-depth gate\n";
        return 1;
    }
    return 0;
}
