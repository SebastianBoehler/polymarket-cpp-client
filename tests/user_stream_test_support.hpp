#pragma once

#include "polymarket/user_stream.hpp"

#include <chrono>
#include <iostream>
#include <thread>

namespace user_stream_test
{
    inline bool check(bool value, const char *name)
    {
        if (!value)
        {
            std::cerr << "failed: " << name << '\n';
        }
        return value;
    }

    template <typename Predicate>
    bool wait_until(Predicate predicate,
                    std::chrono::milliseconds timeout = std::chrono::seconds(2))
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (predicate()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return predicate();
    }

    inline polymarket::ApiCredentials test_credentials()
    {
        return {"key", "secret", "passphrase"};
    }

    inline constexpr const char *order_message =
        R"({"event_type":"order","id":"0xorder","owner":"owner-key","market":"cond-1","asset_id":"yes","side":"buy","original_size":"10","size_matched":"0","price":"0.42","type":"PLACEMENT","status":"LIVE","order_type":"GTC","associate_trades":null,"outcome":"Yes","created_at":"1782753357","expiration":"0","timestamp":"1782753357257"})";

    inline constexpr const char *trade_message =
        R"([{"event_type":"trade","id":"trade-1","taker_order_id":"0xtaker","market":"cond-1","asset_id":"yes","side":"SELL","size":"5","price":"0.42","status":"MATCHED","owner":"owner-key","fee_rate_bps":"0","matchtime":"1782753357","last_update":"1782753358","timestamp":1782753357257,"trader_side":"maker","bucket_index":2,"maker_orders":[{"order_id":"0xorder","owner":"owner-key","matched_amount":"5","price":"0.42","token_id":"yes","side":"buy","outcome":"Yes"}]}])";

    bool protocol_tests();
    bool stream_tests();
    bool recovery_ordering_tests();
    bool delayed_handshake_tests();
    bool authentication_failure_tests(bool reject_during_run);
}
