#pragma once

#include "evm_indexer_adversarial_tests.hpp"
#include <algorithm>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

inline void run_delivery_error_flood_test()
{
    using namespace polymarket;
    const auto path = std::filesystem::temp_directory_path() /
                      "polymarket_cpp_client_delivery_error_flood.json";
    std::filesystem::remove(path);
    auto store = std::make_shared<FileBlockCursorStore>(path.string());
    store->save("error-flood", 10);
    auto transport = std::make_shared<FakeEvmEventTransport>();
    transport->head = "0xa";

    std::mutex callback_mutex;
    std::condition_variable callback_cv;
    bool first_callback_entered = false;
    bool release_first_callback = false;
    std::vector<std::string> observed;
    bool summary_seen = false;
    {
        EvmEventIndexerConfig config;
        config.cursor_name = "error-flood";
        EvmEventIndexer indexer(config, store, transport);
        indexer.on_error([&](const std::string &error)
                         {
            std::unique_lock<std::mutex> lock(callback_mutex);
            observed.push_back(error);
            if (!first_callback_entered)
            {
                first_callback_entered = true;
                callback_cv.notify_all();
                callback_cv.wait(lock, [&]() { return release_first_callback; });
            }
            callback_cv.notify_all(); });
        evm_adversarial_require(indexer.start_live(), "error-flood indexer should start");
        transport->emit_error("initial transport error");
        {
            std::unique_lock<std::mutex> lock(callback_mutex);
            evm_adversarial_require(
                callback_cv.wait_for(lock, std::chrono::seconds(1),
                                     [&]() { return first_callback_entered; }),
                "first error callback should block delivery");
        }

        constexpr uint64_t flood_count = 200;
        for (uint64_t index = 0; index < flood_count; ++index)
            transport->emit_error("transport error " + std::to_string(index));
        {
            std::lock_guard<std::mutex> lock(callback_mutex);
            release_first_callback = true;
        }
        callback_cv.notify_all();
        summary_seen = wait_for_indexer([&]()
                                        {
            std::lock_guard<std::mutex> lock(callback_mutex);
            return std::any_of(observed.begin(), observed.end(), [](const std::string &error)
                               { return error.rfind("EVM delivery errors coalesced: ", 0) == 0; }); });
        indexer.stop();
    }

    uint64_t coalesced = 0;
    bool retained_latest = false;
    for (const auto &error : observed)
    {
        constexpr const char *summary_prefix = "EVM delivery errors coalesced: ";
        if (error.rfind(summary_prefix, 0) == 0)
            coalesced = std::stoull(error.substr(std::char_traits<char>::length(summary_prefix)));
        retained_latest |= error == "transport error 199";
    }
    evm_adversarial_require(summary_seen && coalesced > 0,
                            "error overflow must surface an explicit aggregate count");
    evm_adversarial_require(observed.size() < 201,
                            "slow error callbacks must not retain every queued detail");
    evm_adversarial_require(observed.size() - 1 + coalesced == 201,
                            "error detail and aggregate counts must cover every failure");
    evm_adversarial_require(retained_latest,
                            "bounded error detail must retain the most recent failure");
    std::filesystem::remove(path);
}
