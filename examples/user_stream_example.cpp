#include "clob_client.hpp"
#include "user_stream.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

int main(int argc, char **argv)
{
    using namespace polymarket;

    const char *pk_env = std::getenv("PRIVATE_KEY");
    if (!pk_env)
    {
        std::cout << "PRIVATE_KEY not set; skipping user stream example.\n";
        return 0;
    }

    try
    {
        // Derive L2 API credentials from the signer (no orders are placed).
        ClobClient client("https://clob.polymarket.com", 137, pk_env);
        const auto credentials = client.create_or_derive_api_key();

        UserStream stream(Config{}, credentials);
        stream.on_order([](const UserOrderEvent &order)
                        { std::cout << "[order] " << order.type << ' ' << order.side << ' '
                                    << order.size_matched << '/' << order.original_size
                                    << " @ " << order.price << " id=" << order.id << '\n'; });
        stream.on_trade([](const UserTradeEvent &trade)
                        { std::cout << "[trade] " << trade.status << ' ' << trade.side << ' '
                                    << trade.size << " @ " << trade.price << " id=" << trade.id << '\n'; });
        stream.on_stream_gap([]
                             { std::cout << "[gap] events may have been missed; reconcile via REST\n"; });

        // Optional condition IDs narrow the stream; none means all markets.
        if (argc > 1)
        {
            for (int i = 1; i < argc; ++i)
                stream.subscribe(argv[i]);
        }
        else
        {
            stream.subscribe_all_markets();
        }

        if (!stream.connect())
        {
            std::cerr << "failed to connect user stream\n";
            return 1;
        }

        std::this_thread::sleep_for(std::chrono::seconds(60));
        stream.stop();
        std::cout << "orders=" << stream.order_events()
                  << " trades=" << stream.trade_events() << '\n';
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
