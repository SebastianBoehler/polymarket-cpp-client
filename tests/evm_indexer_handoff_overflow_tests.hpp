#pragma once

#include "evm_indexer_adversarial_tests.hpp"
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <future>
#include <mutex>
#include <string>
#include <vector>

inline void run_handoff_buffer_overflow_test()
{
    using namespace polymarket;
    const auto path = std::filesystem::temp_directory_path() /
                      "polymarket_cpp_client_handoff_overflow_test.json";
    std::filesystem::remove(path);
    auto store = std::make_shared<FileBlockCursorStore>(path.string());
    store->save("handoff-overflow", 10);

    auto transport = std::make_shared<FakeEvmEventTransport>();
    transport->head = "0xc";
    transport->logs = {
        make_indexer_log(11, 0, 0, "0xcanonical-11"),
        make_indexer_log(12, 0, 0, "0xcanonical-12"),
        make_indexer_log(13, 0, 0, "0xcanonical-13"),
        make_indexer_log(14, 0, 0, "0xcanonical-14")};

    std::mutex scan_mutex;
    std::condition_variable scan_cv;
    bool first_scan_entered = false;
    bool release_first_scan = false;
    std::atomic<uint32_t> scans{0};
    transport->during_get_logs = [&](const EvmLogFilter &)
    {
        if (++scans != 1)
            return;
        std::unique_lock<std::mutex> lock(scan_mutex);
        first_scan_entered = true;
        scan_cv.notify_all();
        scan_cv.wait(lock, [&]() { return release_first_scan; });
    };

    EvmEventIndexerConfig config;
    config.cursor_name = "handoff-overflow";
    config.confirmations = 2;
    std::atomic<bool> overflow_reported{false};
    std::vector<std::string> delivered;
    {
        EvmEventIndexer indexer(config, store, transport);
        indexer.on_error([&](const std::string &error)
                         { overflow_reported = error.find("handoff buffer overflow") != std::string::npos; });
        indexer.on_log([&](const EvmIndexedLog &item)
                       { delivered.push_back(item.log.transaction_hash); });

        auto starting = std::async(std::launch::async, [&]() { return indexer.start_live(); });
        {
            std::unique_lock<std::mutex> lock(scan_mutex);
            evm_adversarial_require(
                scan_cv.wait_for(lock, std::chrono::seconds(1),
                                 [&]() { return first_scan_entered; }),
                "initial HTTP catch-up should block after subscribing");
        }

        for (uint64_t index = 0; index < 1025; ++index)
            transport->emit(make_indexer_log(13, 1, index,
                                             "0xflood-" + std::to_string(index)));
        transport->head = "0xe";
        {
            std::lock_guard<std::mutex> lock(scan_mutex);
            release_first_scan = true;
        }
        scan_cv.notify_all();

        const bool start_returned =
            starting.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
        if (!start_returned)
            indexer.stop();
        evm_adversarial_require(start_returned,
                                "overflow recovery should complete the initial handoff");
        evm_adversarial_require(starting.get(),
                                "overflow recovery should keep the live indexer usable");
        indexer.stop();
    }

    evm_adversarial_require(overflow_reported.load(),
                            "bounded handoff overflow must be reported");
    evm_adversarial_require(
        delivered == std::vector<std::string>{"0xcanonical-11", "0xcanonical-12",
                                               "0xcanonical-13", "0xcanonical-14"},
        "overflow recovery must deliver canonical logs once without flood data");
    const auto cursor = store->load_cursor("handoff-overflow");
    evm_adversarial_require(cursor && cursor->block_number == 12 && cursor->block_complete,
                            "overflow recovery must persist only the durable boundary");
    std::filesystem::remove(path);
}
