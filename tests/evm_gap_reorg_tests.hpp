#pragma once

#include "evm_indexer_adversarial_tests.hpp"
#include <filesystem>
#include <mutex>
#include <vector>

inline void run_gap_reorg_replacement_test()
{
    using namespace polymarket;
    const auto path = std::filesystem::temp_directory_path() /
                      "polymarket_cpp_client_gap_reorg_test.json";
    std::filesystem::remove(path);
    auto store = std::make_shared<FileBlockCursorStore>(path.string());
    store->save("gap-reorg", 9);
    auto transport = std::make_shared<FakeEvmEventTransport>();
    transport->head = "0xa";
    const auto orphan = make_indexer_log(10, 0, 0, "0xorphan");
    const auto replacement = make_indexer_log(10, 0, 0, "0xreplacement-after-gap");
    transport->logs = {orphan};
    std::atomic<int> scans{0};
    transport->during_get_logs = [&](const EvmLogFilter &) { ++scans; };

    EvmEventIndexerConfig config;
    config.cursor_name = "gap-reorg";
    config.start_block = 10;
    config.reorg_lookback_blocks = 1;
    std::mutex events_mutex;
    std::vector<std::pair<std::string, bool>> events;
    {
        EvmEventIndexer indexer(config, store, transport);
        indexer.on_log([&](const EvmIndexedLog &item)
                       {
                           std::lock_guard<std::mutex> lock(events_mutex);
                           events.emplace_back(item.log.transaction_hash,
                                               item.log.removed);
                       });
        evm_adversarial_require(indexer.start_live(),
                                "gap-reorg indexer should start");
        evm_adversarial_require(wait_for_cursor(store, "gap-reorg", 10, true),
                                "initial canonical log should be checkpointed");
        transport->logs = {replacement};
        transport->emit_error("EVM websocket stream gap detected at generation 2");
        evm_adversarial_require(
            wait_for_indexer([&]()
                             {
                                 std::lock_guard<std::mutex> lock(events_mutex);
                                 return events.size() == 3;
                             }),
            "gap overlap must retract an orphan and deliver its replacement");
    }
    {
        std::lock_guard<std::mutex> lock(events_mutex);
        evm_adversarial_require(
            events == std::vector<std::pair<std::string, bool>>{
                          {"0xorphan", false}, {"0xorphan", true},
                          {"0xreplacement-after-gap", false}},
            "orphan retraction must precede the canonical replacement");
    }
    evm_adversarial_require(scans.load() >= 2,
                            "gap recovery must rescan a bounded overlap");
    std::filesystem::remove(path);
}
