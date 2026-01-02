#include "websocket_client.hpp"
#include <iostream>
#include <thread>
#include <chrono>

int main()
{
    using namespace polymarket;

    WebSocketClient ws;
    ws.set_url("wss://ws-live-data.polymarket.com");
    ws.set_ping_interval_ms(5000);
    ws.set_auto_reconnect(false);

    ws.on_connect([]()
                  { std::cout << "[ws] connected\n"; });
    ws.on_disconnect([]()
                     { std::cout << "[ws] disconnected\n"; });
    ws.on_error([](const std::string &err)
                { std::cerr << "[ws] error: " << err << "\n"; });
    ws.on_message([](const std::string &msg)
                  {
        // Print only small messages
        std::cout << "[ws] message: " << msg.substr(0, 120) << (msg.size() > 120 ? "..." : "") << "\n"; });

    if (!ws.connect())
    {
        std::cerr << "failed to start websocket" << std::endl;
        return 1;
    }

    // Send a small subscription for orderbook agg (matches rtds usage)
    std::string subscribe = R"({"action":"subscribe","subscriptions":[{"topic":"clob_market","type":"agg_orderbook","filters":"[]"}]})";
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    ws.send(subscribe);

    std::this_thread::sleep_for(std::chrono::seconds(2));
    ws.disconnect();
    return 0;
}
