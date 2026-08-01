#pragma once

#include "evm_indexer_adversarial_tests.hpp"
#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

inline void run_failed_retraction_retry_test()
{
    using namespace polymarket;
    const auto path = std::filesystem::temp_directory_path() /
                      "polymarket_cpp_client_failed_retraction.json";
    std::filesystem::remove(path);
    auto store = std::make_shared<FileBlockCursorStore>(path.string());
    store->save("failed-retraction", 9);
    auto transport = std::make_shared<FakeEvmEventTransport>();
    transport->head = "0xa";
    const auto orphan = make_indexer_log(10, 0, 0, "0xfailed-orphan");
    const auto replacement = make_indexer_log(10, 0, 0, "0xretry-replacement");
    transport->logs = {orphan};
    EvmEventIndexerConfig config;
    config.cursor_name = "failed-retraction";
    config.start_block = 10;
    config.reorg_lookback_blocks = 1;
    std::atomic<int> removal_attempts{0};
    std::atomic<bool> callback_failure_reported{false};
    std::atomic<bool> replacement_delivered{false};
    std::mutex order_mutex;
    std::vector<std::string> order;
    {
        EvmEventIndexer indexer(config, store, transport);
        indexer.on_log([&](const EvmIndexedLog &item)
                       {
                           if (item.log.transaction_hash == orphan.transaction_hash &&
                               item.log.removed)
                           {
                               const auto attempt = ++removal_attempts;
                               {
                                   std::lock_guard lock(order_mutex);
                                   order.push_back("remove-" + std::to_string(attempt));
                               }
                               if (attempt == 1)
                                   throw std::runtime_error("first retraction callback failed");
                           }
                           else if (item.log.transaction_hash == replacement.transaction_hash)
                           {
                               std::lock_guard lock(order_mutex);
                               order.push_back("replacement");
                               replacement_delivered = true;
                           }
                       });
        indexer.on_error([&](const std::string &error)
                         {
                             if (error.find("first retraction callback failed") !=
                                 std::string::npos)
                                 callback_failure_reported = true;
                         });
        evm_adversarial_require(indexer.start_live(),
                                "failed-retraction indexer should start");
        transport->logs = {replacement};
        transport->emit_error("EVM websocket stream gap detected at generation 2");
        evm_adversarial_require(
            wait_for_indexer([&] { return callback_failure_reported.load(); }),
            "first synthetic retraction callback should fail observably");
        transport->emit_head(10);
        evm_adversarial_require(
            wait_for_indexer([&]
                             { return removal_attempts.load() == 2 &&
                                      replacement_delivered.load(); }),
            "failed retraction must be restored and retried by overlap recovery");
    }
    {
        std::lock_guard lock(order_mutex);
        evm_adversarial_require(
            order == std::vector<std::string>{"remove-1", "remove-2", "replacement"},
            "retried orphan removal must precede its replacement");
    }
    std::filesystem::remove(path);
}

inline void run_async_callback_recovery_test()
{
    using namespace polymarket;
    const auto path = std::filesystem::temp_directory_path() /
                      "polymarket_cpp_client_async_callback_recovery.json";
    std::filesystem::remove(path);
    auto store = std::make_shared<FileBlockCursorStore>(path.string());
    store->save("async-callback", 10);
    auto transport = std::make_shared<FakeEvmEventTransport>();
    transport->head = "0xa";
    const auto canonical = make_indexer_log(10, 0, 0, "0xasync-retry");
    std::atomic<int> attempts{0};
    std::atomic<bool> failure_reported{false};
    EvmEventIndexerConfig config;
    config.cursor_name = "async-callback";
    config.start_block = 10;
    config.reorg_lookback_blocks = 1;
    {
        EvmEventIndexer indexer(config, store, transport);
        indexer.on_log([&](const EvmIndexedLog &item)
                       {
                           if (item.log.transaction_hash != canonical.transaction_hash)
                               return;
                           if (++attempts == 1)
                               throw std::runtime_error("async live callback failed");
                       });
        indexer.on_error([&](const std::string &error)
                         {
                             if (error.find("async live callback failed") != std::string::npos)
                                 failure_reported = true;
                         });
        evm_adversarial_require(indexer.start_live(),
                                "async-callback indexer should start");
        transport->logs = {canonical};
        transport->emit(canonical);
        evm_adversarial_require(wait_for_indexer([&] { return failure_reported.load(); }),
                                "async callback failure should be reported");
        evm_adversarial_require(wait_for_indexer([&] { return attempts.load() == 2; }),
                                "async callback failure must schedule overlap recovery");
    }
    std::filesystem::remove(path);
}

inline void run_failed_overlap_retry_test()
{
    using namespace polymarket;
    const auto path = std::filesystem::temp_directory_path() /
                      "polymarket_cpp_client_failed_overlap.json";
    std::filesystem::remove(path);
    auto store = std::make_shared<FileBlockCursorStore>(path.string());
    store->save("failed-overlap", 9);
    auto transport = std::make_shared<FakeEvmEventTransport>();
    transport->head = "0xa";
    const auto orphan = make_indexer_log(10, 0, 0, "0xrpc-orphan");
    const auto replacement = make_indexer_log(10, 0, 0, "0xrpc-replacement");
    transport->logs = {orphan};
    std::atomic<int> scans{0};
    std::atomic<bool> scan_failure_reported{false};
    std::atomic<bool> removed_delivered{false};
    std::atomic<bool> replacement_delivered{false};
    transport->during_get_logs = [&](const EvmLogFilter &)
    {
        if (++scans == 2)
            throw std::runtime_error("first overlap scan failed");
    };
    EvmEventIndexerConfig config;
    config.cursor_name = "failed-overlap";
    config.start_block = 10;
    config.reorg_lookback_blocks = 1;
    {
        EvmEventIndexer indexer(config, store, transport);
        indexer.on_log([&](const EvmIndexedLog &item)
                       {
                           if (item.log.removed &&
                               item.log.transaction_hash == orphan.transaction_hash)
                               removed_delivered = true;
                           if (item.log.transaction_hash == replacement.transaction_hash)
                               replacement_delivered = true;
                       });
        indexer.on_error([&](const std::string &error)
                         {
                             if (error.find("first overlap scan failed") != std::string::npos)
                                 scan_failure_reported = true;
                         });
        evm_adversarial_require(indexer.start_live(),
                                "failed-overlap indexer should start");
        transport->logs = {replacement};
        transport->emit_error("EVM websocket stream gap detected at generation 2");
        evm_adversarial_require(wait_for_indexer([&] { return scan_failure_reported.load(); }),
                                "overlap HTTP failure should be reported");
        transport->emit_head(10);
        evm_adversarial_require(
            wait_for_indexer([&]
                             { return removed_delivered.load() &&
                                      replacement_delivered.load(); }),
            "next head must retry failed overlap before replacement delivery");
    }
    evm_adversarial_require(scans.load() >= 3,
                            "failed overlap intent must survive until the next trigger");
    std::filesystem::remove(path);
}

inline void run_throwing_error_callback_test()
{
    using namespace polymarket;
    const auto path = std::filesystem::temp_directory_path() /
                      "polymarket_cpp_client_throwing_error_callback.json";
    std::filesystem::remove(path);
    auto store = std::make_shared<FileBlockCursorStore>(path.string());
    store->save("throwing-error", 10);
    auto transport = std::make_shared<FakeEvmEventTransport>();
    transport->head = "0xa";
    const auto recovered = make_indexer_log(10, 0, 0, "0xerror-recovery");
    std::atomic<int> scans{0};
    std::atomic<int> error_attempts{0};
    std::atomic<bool> first_scan_failed{false};
    std::atomic<bool> recovery_delivered{false};
    EvmEventIndexerConfig config;
    config.cursor_name = "throwing-error";
    config.start_block = 10;
    config.reorg_lookback_blocks = 1;
    {
        EvmEventIndexer indexer(config, store, transport);
        indexer.on_log([&](const EvmIndexedLog &item)
                       {
                           if (item.log.transaction_hash == recovered.transaction_hash)
                               recovery_delivered = true;
                       });
        indexer.on_error([&](const std::string &)
                         {
                             ++error_attempts;
                             throw std::runtime_error("error callback must not escape");
                         });
        evm_adversarial_require(indexer.start_live(),
                                "throwing-error indexer should start");
        transport->logs = {recovered};
        transport->during_get_logs = [&](const EvmLogFilter &)
        {
            if (++scans == 1)
            {
                first_scan_failed = true;
                throw std::runtime_error("overlap scan failed for error callback test");
            }
        };
        transport->emit_error("EVM websocket stream gap detected at generation 2");
        evm_adversarial_require(wait_for_indexer([&] { return first_scan_failed.load(); }),
                                "overlap scan should reach its injected failure");
        transport->emit_head(10);
        evm_adversarial_require(wait_for_indexer([&] { return recovery_delivered.load(); }),
                                "throwing error callback must not kill later recovery");
    }
    evm_adversarial_require(error_attempts.load() > 0,
                            "background failure must still invoke the error callback");
    std::filesystem::remove(path);
}
