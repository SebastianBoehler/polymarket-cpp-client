#include "clob_client.hpp"
#include <iostream>

int main()
{
    using namespace polymarket;

    try
    {
        http_global_init();
        ClobClient client{"https://clob.polymarket.com", 137};

        auto markets = client.get_markets();
        std::cout << "Fetched markets: " << markets.size() << "\n";
        if (!markets.empty())
        {
            const auto &m = markets.front();
            std::cout << "First market: " << m.market_slug << " (" << m.condition_id << ")\n";
        }
        http_global_cleanup();
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
