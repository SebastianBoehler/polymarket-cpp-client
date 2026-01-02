#include "clob_client.hpp"
#include <iostream>
#include <iomanip>

using namespace polymarket;

int main()
{
    std::cout << "╔══════════════════════════════════════════════════════════════╗\n"
              << "║           ClobClient API Test (Public Endpoints)             ║\n"
              << "╚══════════════════════════════════════════════════════════════╝\n\n";

    // Initialize HTTP globally
    http_global_init();

    // Create public client (no authentication)
    ClobClient client("https://clob.polymarket.com", 137);
    client.set_timeout_ms(10000);

    // Test 1: Get server time
    std::cout << "1. Testing get_server_time()..." << std::endl;
    auto server_time = client.get_server_time();
    if (server_time)
    {
        std::cout << "   ✓ Server time: " << *server_time << std::endl;
    }
    else
    {
        std::cout << "   ✗ Failed to get server time" << std::endl;
    }

    // Test 2: Get markets
    std::cout << "\n2. Testing get_markets()..." << std::endl;
    auto markets = client.get_markets();
    std::cout << "   ✓ Fetched " << markets.size() << " markets" << std::endl;

    if (!markets.empty())
    {
        const auto &m = markets[0];
        std::cout << "   First market: " << m.question.substr(0, 60) << "..." << std::endl;
        std::cout << "   Condition ID: " << m.condition_id << std::endl;

        // Test 3: Get orderbook for first market's YES token
        if (!m.token_yes().empty())
        {
            std::cout << "\n3. Testing get_order_book()..." << std::endl;
            auto book = client.get_order_book(m.token_yes());
            if (book)
            {
                std::cout << "   ✓ Orderbook for YES token:" << std::endl;
                std::cout << "     Best bid: " << std::fixed << std::setprecision(4) << book->best_bid() << std::endl;
                std::cout << "     Best ask: " << std::fixed << std::setprecision(4) << book->best_ask() << std::endl;
                std::cout << "     Bid levels: " << book->bids.size() << std::endl;
                std::cout << "     Ask levels: " << book->asks.size() << std::endl;
            }
            else
            {
                std::cout << "   ✗ Failed to get orderbook" << std::endl;
            }

            // Test 4: Get midpoint
            std::cout << "\n4. Testing get_midpoint()..." << std::endl;
            auto midpoint = client.get_midpoint(m.token_yes());
            if (midpoint)
            {
                std::cout << "   ✓ Midpoint: " << std::fixed << std::setprecision(4) << midpoint->mid << std::endl;
            }
            else
            {
                std::cout << "   ✗ Failed to get midpoint" << std::endl;
            }

            // Test 5: Get spread
            std::cout << "\n5. Testing get_spread()..." << std::endl;
            auto spread = client.get_spread(m.token_yes());
            if (spread)
            {
                std::cout << "   ✓ Spread: " << std::fixed << std::setprecision(4) << spread->spread << std::endl;
            }
            else
            {
                std::cout << "   ✗ Failed to get spread" << std::endl;
            }

            // Test 6: Get tick size
            std::cout << "\n6. Testing get_tick_size()..." << std::endl;
            auto tick_size = client.get_tick_size(m.token_yes());
            if (tick_size)
            {
                std::cout << "   ✓ Tick size: " << tick_size->minimum_tick_size << std::endl;
            }
            else
            {
                std::cout << "   ✗ Failed to get tick size" << std::endl;
            }

            // Test 7: Get neg_risk
            std::cout << "\n7. Testing get_neg_risk()..." << std::endl;
            auto neg_risk = client.get_neg_risk(m.token_yes());
            if (neg_risk)
            {
                std::cout << "   ✓ Neg risk: " << (neg_risk->neg_risk ? "true" : "false") << std::endl;
            }
            else
            {
                std::cout << "   ✗ Failed to get neg_risk" << std::endl;
            }

            // Test 8: Get price
            std::cout << "\n8. Testing get_price()..." << std::endl;
            auto price = client.get_price(m.token_yes(), "buy");
            if (price)
            {
                std::cout << "   ✓ Buy price: " << std::fixed << std::setprecision(4) << price->price << std::endl;
            }
            else
            {
                std::cout << "   ✗ Failed to get price" << std::endl;
            }

            // Test 9: Get last trade price
            std::cout << "\n9. Testing get_last_trade_price()..." << std::endl;
            auto last_price = client.get_last_trade_price(m.token_yes());
            if (last_price)
            {
                std::cout << "   ✓ Last trade price: " << std::fixed << std::setprecision(4) << last_price->price << std::endl;
            }
            else
            {
                std::cout << "   ✗ Failed to get last trade price" << std::endl;
            }
        }
    }

    // Test 10: Get rewards markets
    std::cout << "\n10. Testing get_rewards_markets_current()..." << std::endl;
    auto rewards = client.get_rewards_markets_current();
    std::cout << "    ✓ Found " << rewards.size() << " reward markets" << std::endl;

    std::cout << "\n╔══════════════════════════════════════════════════════════════╗\n"
              << "║                    All Tests Completed!                       ║\n"
              << "╚══════════════════════════════════════════════════════════════╝\n";

    http_global_cleanup();
    return 0;
}
