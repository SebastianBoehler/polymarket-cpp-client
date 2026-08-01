#include "orderbook.hpp"
#include "websocket_test_server.hpp"

#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

using namespace polymarket;
using namespace std::chrono_literals;

namespace
{
    struct Owner
    {
        std::unique_ptr<OrderbookManager> manager;
    };

    struct Counts
    {
        std::atomic<unsigned int> updates{0};
        std::atomic<unsigned int> legacy{0};
        std::atomic<unsigned int> snapshots{0};
        std::atomic<unsigned int> permits{0};
        std::atomic<unsigned int> resets{0};
    };

    Config local_config(const websocket_test::LocalWebSocketServer &server)
    {
        Config config;
        config.clob_ws_url = server.url();
        config.ws_connect_timeout_ms = 1'000;
        config.max_book_age_ms = 60'000;
        config.trigger_combined = 0.98;
        return config;
    }

    MarketState test_market()
    {
        MarketState market;
        market.slug = "owner-reset";
        market.condition_id = "condition";
        market.token_yes = "yes";
        market.token_no = "no";
        market.minimum_order_size = 1.0;
        market.minimum_tick_size = "0.01";
        return market;
    }

    constexpr const char *arb_stream_message =
        R"([{"event_type":"book","asset_id":"yes","bids":[],"asks":[{"price":"0.45","size":"100"}]},{"event_type":"book","asset_id":"no","bids":[],"asks":[{"price":"0.48","size":"100"}]},{"event_type":"book","asset_id":"yes","bids":[],"asks":[{"price":"0.44","size":"100"}]}])";

    template <typename Predicate>
    bool wait_until(Predicate predicate, std::chrono::milliseconds timeout = 1s)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (predicate()) return true;
            std::this_thread::sleep_for(2ms);
        }
        return predicate();
    }

    bool send_when_connected(OrderbookManager &manager,
                             websocket_test::LocalWebSocketServer &server)
    {
        manager.subscribe(test_market());
        return manager.connect() && server.send_to_clients(arb_stream_message);
    }

    bool update_owner_reset()
    {
        websocket_test::LocalWebSocketServer server;
        auto owner = std::make_shared<Owner>();
        auto counts = std::make_shared<Counts>();
        owner->manager = std::make_unique<OrderbookManager>(local_config(server));
        auto *manager = owner->manager.get();
        manager->on_orderbook_update(
            [owner, counts](const std::string &, const Orderbook &)
            {
                counts->updates++;
                owner->manager.reset();
                counts->resets++;
            });
        manager->on_arb_opportunity(
            [counts](const LiveMarketState &, double) { counts->legacy++; });
        manager->on_arb_opportunity_snapshot(
            [counts](const MarketState &, double) { counts->snapshots++; });
        manager->on_arb_opportunity_with_permit(
            [counts](const MarketState &, double, StreamGenerationPermit)
            { counts->permits++; });

        if (!send_when_connected(*manager, server) ||
            !wait_until([&] { return counts->resets.load() == 1; }))
            return false;
        std::this_thread::sleep_for(100ms);
        return counts->updates.load() == 1 && counts->legacy.load() == 0 &&
               counts->snapshots.load() == 0 && counts->permits.load() == 0;
    }

    enum class ArbReset
    {
        Legacy,
        Snapshot,
        Permit
    };

    bool arb_owner_reset(ArbReset reset_at)
    {
        websocket_test::LocalWebSocketServer server;
        auto owner = std::make_shared<Owner>();
        auto counts = std::make_shared<Counts>();
        owner->manager = std::make_unique<OrderbookManager>(local_config(server));
        auto *manager = owner->manager.get();
        manager->on_orderbook_update(
            [counts](const std::string &, const Orderbook &) { counts->updates++; });
        manager->on_arb_opportunity(
            [owner, counts, reset_at](const LiveMarketState &, double)
            {
                counts->legacy++;
                if (reset_at == ArbReset::Legacy)
                {
                    owner->manager.reset();
                    counts->resets++;
                }
            });
        manager->on_arb_opportunity_snapshot(
            [owner, counts, reset_at](const MarketState &, double)
            {
                counts->snapshots++;
                if (reset_at == ArbReset::Snapshot)
                {
                    owner->manager.reset();
                    counts->resets++;
                }
            });
        manager->on_arb_opportunity_with_permit(
            [owner, counts, reset_at](const MarketState &, double,
                                      StreamGenerationPermit)
            {
                counts->permits++;
                if (reset_at == ArbReset::Permit)
                {
                    owner->manager.reset();
                    counts->resets++;
                }
            });

        if (!send_when_connected(*manager, server) ||
            !wait_until([&] { return counts->resets.load() == 1; }))
            return false;
        std::this_thread::sleep_for(100ms);
        const unsigned int expected_snapshot = reset_at == ArbReset::Legacy ? 0 : 1;
        const unsigned int expected_permit = reset_at == ArbReset::Permit ? 1 : 0;
        return counts->updates.load() == 2 && counts->legacy.load() == 1 &&
               counts->snapshots.load() == expected_snapshot &&
               counts->permits.load() == expected_permit;
    }

    bool stop_during_update()
    {
        websocket_test::LocalWebSocketServer server;
        auto manager = std::make_unique<OrderbookManager>(local_config(server));
        auto counts = std::make_shared<Counts>();
        auto *manager_ptr = manager.get();
        manager->on_orderbook_update(
            [manager_ptr, counts](const std::string &, const Orderbook &)
            {
                counts->updates++;
                manager_ptr->stop();
                counts->resets++;
            });
        manager->on_arb_opportunity(
            [counts](const LiveMarketState &, double) { counts->legacy++; });
        manager->on_arb_opportunity_snapshot(
            [counts](const MarketState &, double) { counts->snapshots++; });
        manager->on_arb_opportunity_with_permit(
            [counts](const MarketState &, double, StreamGenerationPermit)
            { counts->permits++; });
        if (!send_when_connected(*manager, server) ||
            !wait_until([&] { return counts->resets.load() >= 1; }))
            return false;
        std::this_thread::sleep_for(100ms);
        manager->stop();
        return counts->updates.load() == 1 && counts->legacy.load() == 0 &&
               counts->snapshots.load() == 0 && counts->permits.load() == 0;
    }

    bool setter_during_dispatch()
    {
        websocket_test::LocalWebSocketServer server;
        OrderbookManager manager(local_config(server));
        std::promise<void> entered;
        std::promise<void> resume;
        std::promise<void> completed;
        auto resume_future = resume.get_future().share();
        manager.on_orderbook_update(
            [&](const std::string &, const Orderbook &)
            {
                entered.set_value();
                resume_future.wait();
                completed.set_value();
            });
        manager.subscribe(test_market());
        if (!manager.connect() || !server.send_to_clients(
                R"({"event_type":"book","asset_id":"yes","bids":[],"asks":[{"price":"0.45","size":"100"}]})"))
            return false;
        auto entered_future = entered.get_future();
        if (entered_future.wait_for(1s) != std::future_status::ready)
            return false;
        std::thread setter([&]
                           { manager.on_orderbook_update(
                                 [](const std::string &, const Orderbook &) {}); });
        setter.join();
        resume.set_value();
        auto completed_future = completed.get_future();
        const bool ok = completed_future.wait_for(1s) == std::future_status::ready;
        manager.stop();
        return ok;
    }
}

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        std::cerr << "expected one scenario\n";
        return 2;
    }
    const std::string scenario = argv[1];
    bool ok = false;
    if (scenario == "update") ok = update_owner_reset();
    else if (scenario == "arb-legacy") ok = arb_owner_reset(ArbReset::Legacy);
    else if (scenario == "arb-snapshot") ok = arb_owner_reset(ArbReset::Snapshot);
    else if (scenario == "arb-permit") ok = arb_owner_reset(ArbReset::Permit);
    else if (scenario == "stop-update") ok = stop_during_update();
    else if (scenario == "setter-dispatch") ok = setter_during_dispatch();
    else
    {
        std::cerr << "unknown scenario: " << scenario << '\n';
        return 2;
    }
    if (!ok) std::cerr << "failed: " << scenario << '\n';
    return ok ? 0 : 1;
}
