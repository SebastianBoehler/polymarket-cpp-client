#include "order_signer.hpp"
#include <chrono>
#include <iostream>

using namespace polymarket;

namespace
{
    constexpr const char *kPrivateKey = "0x0000000000000000000000000000000000000000000000000000000000000001";
    constexpr const char *kExchange = "0xE111180000d2663C0091e4f400237545B87B996B";
    constexpr const char *kZeroBytes32 = "0x0000000000000000000000000000000000000000000000000000000000000000";

    OrderData fixed_order(const std::string &maker)
    {
        OrderData order;
        order.maker = maker;
        order.signer = maker;
        order.taker = "0x0000000000000000000000000000000000000000";
        order.token_id = "1234567890";
        order.maker_amount = "1000000";
        order.taker_amount = "2000000";
        order.side = OrderSide::BUY;
        order.expiration = "0";
        order.timestamp = "1713398400000";
        order.metadata = kZeroBytes32;
        order.builder = kZeroBytes32;
        order.signature_type = SignatureType::EOA;
        return order;
    }
}

int main(int argc, char **argv)
{
    const int iterations = argc > 1 ? std::stoi(argv[1]) : 1000;
    OrderSigner signer(kPrivateKey, 137);
    const auto order = fixed_order(signer.address());

    auto start = std::chrono::steady_clock::now();
    std::string last_signature;
    for (int i = 0; i < iterations; ++i)
    {
        last_signature = signer.sign_order_with_salt(order, kExchange, std::to_string(1000000 + i)).signature;
    }
    auto end = std::chrono::steady_clock::now();
    const auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    std::cout << "benchmark=v2_signing iterations=" << iterations
              << " total_us=" << elapsed_us
              << " avg_us=" << static_cast<double>(elapsed_us) / iterations
              << " last_signature_bytes=" << last_signature.size() << "\n";
    return 0;
}
