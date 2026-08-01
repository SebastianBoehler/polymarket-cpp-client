#pragma once

#include "evm_event_indexer_test_support.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <future>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>

inline void evm_adversarial_require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

template <typename Predicate>
bool wait_for_indexer(Predicate predicate,
                      std::chrono::milliseconds timeout = std::chrono::seconds(2))
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!predicate() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    return predicate();
}

inline bool wait_for_cursor(const std::shared_ptr<polymarket::EvmBlockCursorStore> &store,
                            const std::string &name, uint64_t block, bool complete)
{
    return wait_for_indexer([&]()
                            {
                                const auto cursor = store->load_cursor(name);
                                return cursor && cursor->block_number == block &&
                                       cursor->block_complete == complete;
                            });
}

class LegacyEvmCursorStore final : public polymarket::EvmBlockCursorStore
{
public:
    std::optional<uint64_t> cursor{10};

    std::optional<uint64_t> load(const std::string &) override { return cursor; }
    void save(const std::string &, uint64_t block) override { cursor = block; }
};

inline void run_evm_indexer_adversarial_tests()
{
    using namespace polymarket;

    auto max_store = std::make_shared<LegacyEvmCursorStore>();
    max_store->cursor = std::numeric_limits<uint64_t>::max();
    auto max_transport = std::make_shared<FakeEvmEventTransport>();
    max_transport->head = evm_uint64_to_quantity(std::numeric_limits<uint64_t>::max());
    std::atomic<int> max_cursor_queries{0};
    max_transport->during_get_logs = [&](const EvmLogFilter &) { ++max_cursor_queries; };
    EvmEventIndexerConfig max_config;
    max_config.batch_size = std::numeric_limits<uint64_t>::max();
    EvmEventIndexer max_indexer(max_config, max_store, max_transport);
    const auto max_report = max_indexer.catch_up();
    evm_adversarial_require(max_report.ranges_scanned == 0 && max_cursor_queries == 0,
                            "completed UINT64_MAX cursor must not wrap and rescan from zero");

    for (const std::string invalid : {"-1", "+1", "12junk", "0x", "0x1g", " 1", "1 "})
    {
        bool rejected = false;
        try
        {
            (void)evm_quantity_to_uint64(invalid);
        }
        catch (const std::exception &)
        {
            rejected = true;
        }
        evm_adversarial_require(rejected,
                                "quantity parser must reject signs and trailing garbage");
    }

    auto malformed_transport = std::make_shared<FakeEvmEventTransport>();
    malformed_transport->head = "0x10rpc-garbage";
    bool malformed_rpc_rejected = false;
    try
    {
        EvmEventIndexer indexer({}, nullptr, malformed_transport);
        (void)indexer.catch_up();
    }
    catch (const std::exception &)
    {
        malformed_rpc_rejected = true;
    }
    evm_adversarial_require(malformed_rpc_rejected,
                            "malformed RPC quantity must fail before scanning");

    const auto malformed_path = std::filesystem::temp_directory_path() /
                                "polymarket_cpp_client_malformed_cursor_test.json";
    {
        std::ofstream output(malformed_path);
        output << R"({"bad":"12cursor-garbage"})";
    }
    bool malformed_cursor_rejected = false;
    try
    {
        FileBlockCursorStore store(malformed_path.string());
        (void)store.load_cursor("bad");
    }
    catch (const std::exception &)
    {
        malformed_cursor_rejected = true;
    }
    std::filesystem::remove(malformed_path);
    evm_adversarial_require(malformed_cursor_rejected,
                            "malformed persisted quantity must not be partially parsed");

    // Removed unconfirmed logs are beyond the durable cursor and must not
    // create forward progress.
    const auto ahead_path = std::filesystem::temp_directory_path() /
                            "polymarket_cpp_client_cursor_ahead_reorg_test.json";
    std::filesystem::remove(ahead_path);
    auto ahead_store = std::make_shared<FileBlockCursorStore>(ahead_path.string());
    ahead_store->save("ahead", 10);
    auto ahead_transport = std::make_shared<FakeEvmEventTransport>();
    ahead_transport->head = "0xa";
    EvmEventIndexerConfig ahead_config;
    ahead_config.cursor_name = "ahead";
    {
        EvmEventIndexer indexer(ahead_config, ahead_store, ahead_transport);
        evm_adversarial_require(indexer.start_live(), "ahead-reorg indexer should start");
        auto removed_ahead = make_indexer_log(12, 0, 0, "0xahead");
        removed_ahead.removed = true;
        ahead_transport->emit(removed_ahead);
        const auto cursor = ahead_store->load_cursor("ahead");
        evm_adversarial_require(cursor && cursor->block_complete && cursor->block_number == 10,
                                "removed unconfirmed log must not advance durable cursor");
    }
    std::filesystem::remove(ahead_path);

    // Legacy stores cannot persist a rewind, but that persistence error must
    // not suppress the removed log needed by state projectors.
    auto legacy_store = std::make_shared<LegacyEvmCursorStore>();
    auto legacy_transport = std::make_shared<FakeEvmEventTransport>();
    legacy_transport->head = "0xa";
    std::atomic<int> removed_deliveries{0};
    std::atomic<bool> rewind_error_reported{false};
    {
        EvmEventIndexer indexer({}, legacy_store, legacy_transport);
        indexer.on_log([&](const EvmIndexedLog &item)
                       { removed_deliveries += item.log.removed ? 1 : 0; });
        indexer.on_error([&](const std::string &error)
                         { rewind_error_reported = error.find("rewind") != std::string::npos; });
        evm_adversarial_require(indexer.start_live(), "legacy-store indexer should start");
        auto removed = make_indexer_log(9, 0, 0, "0xlegacy-removed");
        removed.removed = true;
        legacy_transport->emit(removed);
        evm_adversarial_require(
            wait_for_indexer([&]()
                             {
                                 return removed_deliveries.load() == 1 &&
                                        rewind_error_reported.load();
                             }),
            "legacy rewind result should be dispatched");
    }
    evm_adversarial_require(removed_deliveries == 1,
                            "rewind persistence failure must not suppress removed log");
    evm_adversarial_require(rewind_error_reported.load(),
                            "rewind persistence failure must be reported clearly");

    // newHeads must only schedule HTTP reconciliation. Multiple heads arriving
    // while one request is blocked are coalesced into one subsequent scan.
    const auto async_path = std::filesystem::temp_directory_path() /
                            "polymarket_cpp_client_cursor_async_head_test.json";
    std::filesystem::remove(async_path);
    auto async_store = std::make_shared<FileBlockCursorStore>(async_path.string());
    async_store->save("async", 10);
    auto async_transport = std::make_shared<FakeEvmEventTransport>();
    async_transport->head = "0xa";
    EvmEventIndexerConfig async_config;
    async_config.cursor_name = "async";
    std::mutex gate_mutex;
    std::condition_variable gate_cv;
    bool scan_entered = false;
    bool release_scan = false;
    std::atomic<int> scans{0};
    std::atomic<uint64_t> checkpoint{0};
    {
        EvmEventIndexer indexer(async_config, async_store, async_transport);
        indexer.on_checkpoint([&](uint64_t block) { checkpoint = block; });
        evm_adversarial_require(indexer.start_live(), "async-head indexer should start");
        async_transport->during_get_logs = [&](const EvmLogFilter &)
        {
            std::unique_lock<std::mutex> lock(gate_mutex);
            const auto scan = ++scans;
            if (scan == 1)
            {
                scan_entered = true;
                gate_cv.notify_all();
                gate_cv.wait(lock, [&]() { return release_scan; });
            }
        };

        auto emitter = std::async(std::launch::async,
                                  [&]() { async_transport->emit_head(11); });
        {
            std::unique_lock<std::mutex> lock(gate_mutex);
            evm_adversarial_require(
                gate_cv.wait_for(lock, std::chrono::seconds(1), [&]() { return scan_entered; }),
                "head reconciliation scan should begin");
        }
        const bool callback_returned =
            emitter.wait_for(std::chrono::milliseconds(100)) == std::future_status::ready;
        if (callback_returned)
        {
            async_transport->emit_head(12);
            async_transport->emit_head(13);
        }
        {
            std::lock_guard<std::mutex> lock(gate_mutex);
            release_scan = true;
        }
        gate_cv.notify_all();
        emitter.wait();
        evm_adversarial_require(callback_returned,
                                "newHeads callback must not perform synchronous HTTP work");
        evm_adversarial_require(wait_for_indexer([&]() { return checkpoint.load() >= 13; }),
                                "coalesced head reconciliation should reach newest head");
        evm_adversarial_require(scans.load() == 2,
                                "pending heads should coalesce into one subsequent scan");
    }
    std::filesystem::remove(async_path);
}
