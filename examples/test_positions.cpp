#include "polymarket/clob_client.hpp"
#include <iostream>
#include <cstdlib>
#include <fstream>
#include <sstream>

// Load .env file
static void load_env(const std::string &path)
{
    std::ifstream file(path);
    std::string line;
    while (std::getline(file, line))
    {
        // Skip comments and empty lines
        if (line.empty() || line[0] == '#')
            continue;

        // Find the = sign
        auto pos = line.find('=');
        if (pos == std::string::npos)
            continue;

        std::string key = line.substr(0, pos);
        std::string value = line.substr(pos + 1);

        // Remove trailing comments
        auto comment_pos = value.find('#');
        if (comment_pos != std::string::npos)
        {
            value = value.substr(0, comment_pos);
        }

        // Trim whitespace
        while (!value.empty() && (value.back() == ' ' || value.back() == '\t'))
            value.pop_back();

        setenv(key.c_str(), value.c_str(), 1);
    }
}

int main()
{
    using namespace polymarket;

    try
    {
        // Load .env file
        load_env(".env");

        http_global_init();

        std::string funder_address = std::getenv("FUNDER_ADDRESS") ? std::getenv("FUNDER_ADDRESS") : "";

        if (funder_address.empty())
        {
            std::cerr << "FUNDER_ADDRESS not set in .env\n";
            return 1;
        }

        std::cout << "Testing get_positions for address: " << funder_address << "\n\n";

        // Create unauthenticated client (positions API doesn't need auth)
        ClobClient client{"https://clob.polymarket.com", 137};

        // Test get_positions
        std::cout << "=== All Positions ===\n\n";
        auto positions = client.get_positions(funder_address);
        std::cout << "Total positions: " << positions.size() << "\n\n";

        for (const auto &pos : positions)
        {
            std::cout << "Market: " << pos.title << "\n";
            std::cout << "  Outcome: " << pos.outcome << "\n";
            std::cout << "  Size: " << pos.size << " shares\n";
            std::cout << "  Avg Price: $" << pos.avg_price << "\n";
            std::cout << "  Current Price: $" << pos.cur_price << "\n";
            std::cout << "  Initial Value: $" << pos.initial_value << "\n";
            std::cout << "  Current Value: $" << pos.current_value << "\n";
            std::cout << "  Cash P&L: $" << pos.cash_pnl << "\n";
            std::cout << "  Percent P&L: " << pos.percent_pnl << "%\n";
            std::cout << "  Redeemable: " << (pos.redeemable ? "Yes" : "No") << "\n";
            std::cout << "  Mergeable: " << (pos.mergeable ? "Yes" : "No") << "\n";
            std::cout << "  Neg Risk: " << (pos.negative_risk ? "Yes" : "No") << "\n";
            std::cout << "  Token ID: " << pos.asset << "\n";
            std::cout << "  Condition ID: " << pos.condition_id << "\n";
            std::cout << "\n";
        }

        // Test get_redeemable_positions
        std::cout << "=== Redeemable Positions ===\n\n";
        auto redeemable = client.get_redeemable_positions(funder_address);
        std::cout << "Redeemable positions: " << redeemable.size() << "\n";
        for (const auto &pos : redeemable)
        {
            std::cout << "  " << pos.title << " (" << pos.outcome << ") - " << pos.size << " shares @ $" << pos.current_value << "\n";
        }

        // Test get_mergeable_positions
        std::cout << "\n=== Mergeable Positions ===\n\n";
        auto mergeable = client.get_mergeable_positions(funder_address);
        std::cout << "Mergeable positions: " << mergeable.size() << "\n";
        for (const auto &pos : mergeable)
        {
            std::cout << "  " << pos.title << " (" << pos.outcome << ") - " << pos.size << " shares\n";
        }

        http_global_cleanup();
        std::cout << "\n=== Test Complete ===\n";
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
