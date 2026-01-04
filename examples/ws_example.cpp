#include "websocket_client.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>

using json = nlohmann::json;

int main()
{
    using namespace polymarket;

    // Example token IDs (YES/NO tokens for "Will 2025 be the hottest year on record?")
    // In real usage, fetch these from ClobClient::get_market() or MarketFetcher
    const std::string token_yes = "28537688195618790236576003993608298766895159067143553592678106718799385303898";
    const std::string token_no = "57878493050148425637822780001963685814731344602319345842647239312888833935027";

    std::atomic<bool> connected{false};

    WebSocketClient ws;
    ws.set_url("wss://ws-subscriptions-clob.polymarket.com/ws/market");
    ws.set_ping_interval_ms(10000);
    ws.set_auto_reconnect(false);

    ws.on_connect([&connected]()
                  {
        std::cout << "[ws] connected\n";
        connected = true; });
    ws.on_disconnect([]()
                     { std::cout << "[ws] disconnected\n"; });
    ws.on_error([](const std::string &err)
                { std::cerr << "[ws] error: " << err << "\n"; });
    ws.on_message([](const std::string &msg)
                  {
        // Pretty print JSON or truncate if too long
        try {
            auto j = json::parse(msg);
            std::string pretty = j.dump(2);
            if (pretty.size() > 800) {
                std::cout << "[ws] message: " << pretty.substr(0, 800) << "...\n";
            } else {
                std::cout << "[ws] message: " << pretty << "\n";
            }
        } catch (...) {
            std::cout << "[ws] message: " << msg.substr(0, 300) << (msg.size() > 300 ? "..." : "") << "\n";
        } });

    if (!ws.connect())
    {
        std::cerr << "failed to start websocket" << std::endl;
        return 1;
    }

    // Wait for connection to establish
    for (int i = 0; i < 20 && !connected; ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (!connected)
    {
        std::cerr << "connection timeout" << std::endl;
        return 1;
    }

    // Subscribe to market channel with asset IDs (Polymarket CLOB WebSocket format)
    json subscribe_msg;
    subscribe_msg["type"] = "market";
    subscribe_msg["assets_ids"] = json::array({token_yes, token_no});

    std::cout << "[ws] sending subscription: " << subscribe_msg.dump() << "\n";
    ws.send(subscribe_msg.dump());

    // Listen for messages for 10 seconds
    std::cout << "[ws] listening for orderbook updates...\n";
    std::this_thread::sleep_for(std::chrono::seconds(10));

    ws.disconnect();
    std::cout << "[ws] done\n";
    return 0;
}
