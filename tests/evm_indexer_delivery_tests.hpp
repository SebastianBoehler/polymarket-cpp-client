#pragma once

#include "evm_indexer_adversarial_tests.hpp"
#include "evm_gap_reorg_tests.hpp"
#include "evm_gap_failure_tests.hpp"
#include "evm_transport_epoch_tests.hpp"
#include "evm_owner_reset_tests.hpp"
#include "evm_callback_safety_tests.hpp"
#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <mutex>
#include <string>
#include <vector>

inline void run_serial_delivery_test()
{
    using namespace polymarket;
    const auto path = std::filesystem::temp_directory_path() /
                      "polymarket_cpp_client_serial_delivery_test.json";
    std::filesystem::remove(path);
    auto store = std::make_shared<FileBlockCursorStore>(path.string());
    store->save("serial", 10);
    auto transport = std::make_shared<FakeEvmEventTransport>();
    transport->head = "0xa";
    transport->logs = {make_indexer_log(11, 0, 0, "0xhistorical")};

    std::mutex scan_mutex;
    std::condition_variable scan_cv;
    bool scan_entered = false;
    bool release_scan = false;
    transport->during_get_logs = [&](const EvmLogFilter &)
    {
        std::unique_lock<std::mutex> lock(scan_mutex);
        scan_entered = true;
        scan_cv.notify_all();
        scan_cv.wait(lock, [&]() { return release_scan; });
    };

    std::atomic<int> active{0};
    std::atomic<int> max_active{0};
    std::atomic<int> finished{0};
    std::mutex callback_mutex;
    std::condition_variable callback_cv;
    bool historical_entered = false;
    bool allow_live_finish = false;
    std::vector<std::string> order;
    EvmEventIndexerConfig config;
    config.cursor_name = "serial";
    EvmEventIndexer indexer(config, store, transport);
    indexer.on_log([&](const EvmIndexedLog &item)
                   {
                       const auto now = ++active;
                       auto observed = max_active.load();
                       while (observed < now &&
                              !max_active.compare_exchange_weak(observed, now))
                       {
                       }
                       {
                           std::lock_guard<std::mutex> lock(callback_mutex);
                           order.push_back(item.log.transaction_hash);
                           if (item.log.transaction_hash == "0xhistorical")
                           {
                               historical_entered = true;
                               callback_cv.notify_all();
                           }
                       }
                       if (item.log.transaction_hash == "0xlive")
                       {
                           std::unique_lock<std::mutex> lock(callback_mutex);
                           callback_cv.wait(lock, [&]() { return allow_live_finish; });
                       }
                       --active;
                       ++finished;
                       callback_cv.notify_all();
                   });
    evm_adversarial_require(indexer.start_live(), "serial-delivery indexer should start");
    transport->emit_head(11);
    {
        std::unique_lock<std::mutex> lock(scan_mutex);
        evm_adversarial_require(
            scan_cv.wait_for(lock, std::chrono::seconds(1), [&]() { return scan_entered; }),
            "serial-delivery reconciliation should enter HTTP scan");
    }

    auto live_emitter = std::async(std::launch::async, [&]()
                                   { transport->emit(make_indexer_log(12, 0, 0, "0xlive")); });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    {
        std::lock_guard<std::mutex> lock(scan_mutex);
        release_scan = true;
    }
    scan_cv.notify_all();
    {
        std::unique_lock<std::mutex> lock(callback_mutex);
        evm_adversarial_require(
            callback_cv.wait_for(lock, std::chrono::seconds(1),
                                 [&]() { return historical_entered; }),
            "historical callback should be delivered");
        allow_live_finish = true;
    }
    callback_cv.notify_all();
    live_emitter.wait();
    evm_adversarial_require(wait_for_indexer([&]() { return finished.load() == 2; }),
                            "both serialized callbacks should finish");
    evm_adversarial_require(max_active.load() == 1,
                            "historical and live callbacks must never overlap");
    evm_adversarial_require(order == std::vector<std::string>{"0xhistorical", "0xlive"},
                            "reconciled canonical log must precede later live log");
    std::filesystem::remove(path);
}

inline void run_reentrant_stop_test()
{
    using namespace polymarket;
    const auto path = std::filesystem::temp_directory_path() /
                      "polymarket_cpp_client_reentrant_stop_test.json";
    std::filesystem::remove(path);
    auto store = std::make_shared<FileBlockCursorStore>(path.string());
    store->save("reentrant", 10);
    auto transport = std::make_shared<FakeEvmEventTransport>();
    transport->head = "0xa";
    transport->logs = {make_indexer_log(11, 0, 0, "0xstop")};
    EvmEventIndexerConfig config;
    config.cursor_name = "reentrant";
    auto *indexer = new EvmEventIndexer(config, store, transport);
    std::atomic<bool> stop_returned{false};
    indexer->on_log([&](const EvmIndexedLog &)
                    {
                        indexer->stop();
                        stop_returned = true;
                    });
    evm_adversarial_require(indexer->start_live(), "reentrant-stop indexer should start");
    transport->emit_head(11);
    evm_adversarial_require(wait_for_indexer([&]() { return stop_returned.load(); },
                                             std::chrono::milliseconds(300)),
                            "stop called from a log callback must return without deadlock");
    delete indexer;
    std::filesystem::remove(path);
}

inline void run_gap_reconciliation_test()
{
    using namespace polymarket;
    const auto path = std::filesystem::temp_directory_path() /
                      "polymarket_cpp_client_gap_reconciliation_test.json";
    std::filesystem::remove(path);
    auto store = std::make_shared<FileBlockCursorStore>(path.string());
    store->save("gap", 10);
    auto transport = std::make_shared<FakeEvmEventTransport>();
    transport->head = "0xa";
    EvmEventIndexerConfig config;
    config.cursor_name = "gap";
    config.confirmations = 2;
    std::atomic<bool> error_reported{false};
    std::atomic<bool> recovered_log{false};
    {
        EvmEventIndexer indexer(config, store, transport);
        indexer.on_error([&](const std::string &error)
                         { error_reported = error.find("stream gap") != std::string::npos; });
        indexer.on_log([&](const EvmIndexedLog &item)
                       { recovered_log = item.log.transaction_hash == "0xgap-recovery"; });
        evm_adversarial_require(indexer.start_live(), "gap-recovery indexer should start");
        transport->head = "0xc";
        transport->logs = {make_indexer_log(12, 0, 0, "0xgap-recovery")};
        transport->emit_error("EVM websocket stream gap detected");
        evm_adversarial_require(wait_for_indexer([&]() { return recovered_log.load(); }),
                                "gap reconciliation must recover the unconfirmed window");
        const auto cursor = store->load_cursor("gap");
        evm_adversarial_require(cursor && cursor->block_number == 10 && cursor->block_complete,
                                "gap recovery must not persist beyond the durable boundary");
    }
    evm_adversarial_require(error_reported.load(), "stream gap must reach error callback");
    std::filesystem::remove(path);
}

inline void run_delivery_overflow_test()
{
    using namespace polymarket;
    const auto path = std::filesystem::temp_directory_path() /
                      "polymarket_cpp_client_delivery_overflow_test.json";
    std::filesystem::remove(path);
    auto store = std::make_shared<FileBlockCursorStore>(path.string());
    store->save("overflow", 10);
    auto transport = std::make_shared<FakeEvmEventTransport>();
    transport->head = "0xa";
    EvmEventIndexerConfig config;
    config.cursor_name = "overflow";
    std::mutex blocker_mutex;
    std::condition_variable blocker_cv;
    bool blocker_entered = false;
    bool release_blocker = false;
    std::atomic<bool> overflow_reported{false};
    std::atomic<bool> recovery_delivered{false};
    {
        EvmEventIndexer indexer(config, store, transport);
        indexer.on_error([&](const std::string &error)
                         { overflow_reported = error.find("delivery queue overflow") != std::string::npos; });
        indexer.on_log([&](const EvmIndexedLog &item)
                       {
                           if (item.log.transaction_hash == "0xblocker")
                           {
                               std::unique_lock<std::mutex> lock(blocker_mutex);
                               blocker_entered = true;
                               blocker_cv.notify_all();
                               blocker_cv.wait(lock, [&]() { return release_blocker; });
                           }
                           if (item.log.transaction_hash == "0xoverflow-recovery")
                               recovery_delivered = true;
                       });
        evm_adversarial_require(indexer.start_live(), "overflow indexer should start");
        transport->emit(make_indexer_log(12, 0, 0, "0xblocker"));
        {
            std::unique_lock<std::mutex> lock(blocker_mutex);
            evm_adversarial_require(
                blocker_cv.wait_for(lock, std::chrono::seconds(1),
                                    [&]() { return blocker_entered; }),
                "blocking delivery callback should start");
        }
        transport->head = "0xb";
        transport->logs = {make_indexer_log(11, 0, 0, "0xoverflow-recovery")};
        for (uint64_t index = 0; index < 1030; ++index)
            transport->emit(make_indexer_log(12, 0, index + 1,
                                             "0xqueued-" + std::to_string(index)));
        {
            std::lock_guard<std::mutex> lock(blocker_mutex);
            release_blocker = true;
        }
        blocker_cv.notify_all();
        evm_adversarial_require(wait_for_cursor(store, "overflow", 11, true),
                                "delivery overflow must reconcile through HTTP");
        evm_adversarial_require(wait_for_indexer([&]()
                                                 {
                                                     return overflow_reported.load() &&
                                                            recovery_delivered.load();
                                                 }),
                                "delivery overflow must be reported and recovered");
    }
    std::filesystem::remove(path);
}

inline void run_evm_indexer_delivery_tests()
{
    run_gap_reorg_replacement_test();
    run_failed_retraction_retry_test();
    run_async_callback_recovery_test();
    run_failed_overlap_retry_test();
    run_throwing_error_callback_test();
    run_evm_owner_reset_tests();
    run_evm_callback_safety_tests();
    run_evm_transport_epoch_test();
    const auto *focused = std::getenv("EVM_DELIVERY_CASE");
    if (!focused || std::string(focused) == "serial")
        run_serial_delivery_test();
    if (!focused || std::string(focused) == "reentrant")
        run_reentrant_stop_test();
    if (!focused || std::string(focused) == "gap")
        run_gap_reconciliation_test();
    if (!focused || std::string(focused) == "overflow")
        run_delivery_overflow_test();
}
