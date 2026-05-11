#include <chrono>
#include <iostream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

int main(int argc, char **argv)
{
    const int iterations = argc > 1 ? std::stoi(argv[1]) : 100000;
    const std::string message =
        R"({"topic":"clob_market","type":"agg_orderbook","payload":{"asset_id":"token-1","bids":[{"price":"0.41","size":"100"}],"asks":[{"price":"0.42","size":"80"}]}})";

    auto start = std::chrono::steady_clock::now();
    std::size_t levels = 0;
    for (int i = 0; i < iterations; ++i)
    {
        auto parsed = json::parse(message);
        const auto &payload = parsed["payload"];
        levels += payload["bids"].size() + payload["asks"].size();
    }
    auto end = std::chrono::steady_clock::now();
    const auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    std::cout << "benchmark=ws_parse iterations=" << iterations
              << " total_us=" << elapsed_us
              << " avg_us=" << static_cast<double>(elapsed_us) / iterations
              << " levels=" << levels << "\n";
    return 0;
}
