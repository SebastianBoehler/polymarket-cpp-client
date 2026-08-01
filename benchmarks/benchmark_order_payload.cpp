#include "order_signer.hpp"
#include <chrono>
#include <iostream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
using namespace polymarket;

namespace
{
    SignedOrder sample_order()
    {
        SignedOrder order;
        order.salt = "123456789";
        order.maker = "0x1111111111111111111111111111111111111111";
        order.signer = order.maker;
        order.taker = "0x0000000000000000000000000000000000000000";
        order.token_id = "1234567890";
        order.maker_amount = "1000000";
        order.taker_amount = "2000000";
        order.expiration = "0";
        order.side = 0;
        order.signature_type = 0;
        order.timestamp = "1713398400000";
        order.metadata = "0x0000000000000000000000000000000000000000000000000000000000000000";
        order.builder = order.metadata;
        order.signature = "0xabc";
        return order;
    }

    std::string serialize_payload(const SignedOrder &order)
    {
        json body;
        body["order"] = {
            {"salt", std::stoll(order.salt)},
            {"maker", order.maker},
            {"signer", order.signer},
            {"tokenId", order.token_id},
            {"makerAmount", order.maker_amount},
            {"takerAmount", order.taker_amount},
            {"expiration", order.expiration},
            {"side", order.side == 0 ? "BUY" : "SELL"},
            {"signatureType", order.signature_type},
            {"timestamp", order.timestamp},
            {"metadata", order.metadata},
            {"builder", order.builder},
            {"signature", order.signature}};
        body["owner"] = "owner-key";
        body["orderType"] = "GTC";
        body["deferExec"] = false;
        body["postOnly"] = false;
        return body.dump();
    }
}

int main(int argc, char **argv)
{
    const int iterations = argc > 1 ? std::stoi(argv[1]) : 100000;
    const auto order = sample_order();

    auto start = std::chrono::steady_clock::now();
    std::size_t bytes = 0;
    for (int i = 0; i < iterations; ++i)
    {
        bytes += serialize_payload(order).size();
    }
    auto end = std::chrono::steady_clock::now();
    const auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    std::cout << "benchmark=order_payload iterations=" << iterations
              << " total_us=" << elapsed_us
              << " avg_us=" << static_cast<double>(elapsed_us) / iterations
              << " bytes=" << bytes << "\n";
    return 0;
}
