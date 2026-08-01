#include "evm_event_indexer.hpp"
#include "evm_indexer_adversarial_tests.hpp"
#include "evm_indexer_delivery_tests.hpp"
#include "evm_indexer_handoff_overflow_tests.hpp"
#include "evm_cursor_concurrency_tests.hpp"
#include "evm_delivery_error_tests.hpp"
#include "evm_event_indexer_test_support.hpp"
#include <atomic>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <type_traits>
#include <vector>
namespace
{
void require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}
} // namespace

int main()
{
    using namespace polymarket;
    static_assert(std::is_copy_constructible_v<FileBlockCursorStore>);

    require(evm_quantity_to_uint64("0x10") == 16, "hex quantity parse failed");
    require(evm_quantity_to_uint64("42") == 42, "decimal quantity parse failed");
    require(evm_uint64_to_quantity(0) == "0x0", "zero quantity format failed");
    require(evm_uint64_to_quantity(255) == "0xff", "quantity format failed");
    auto ranges = evm_make_block_ranges(10, 25, 8);
    require(ranges.size() == 2, "range count failed");
    require(ranges[0].from_block == 10, "first range start failed");
    require(ranges[0].to_block == 17, "first range end failed");
    require(ranges[1].from_block == 18, "second range start failed");
    require(ranges[1].to_block == 25, "second range end failed");
    require(evm_make_block_ranges(30, 20, 8).empty(), "empty range failed");

    auto failed_transport = std::make_shared<FakeEvmEventTransport>();
    failed_transport->start_result = false;
    EvmEventIndexerConfig failed_config;
    std::atomic<int> failed_start_deliveries{0};
    {
        EvmEventIndexer indexer(failed_config, nullptr, failed_transport);
        indexer.on_log([&failed_start_deliveries](const EvmIndexedLog &)
                       { ++failed_start_deliveries; });
        require(!indexer.start_live(), "transport start failure must propagate");
        require(failed_transport->stop_calls == 1, "failed start must stop its transport");
        failed_transport->emit_from(0, make_indexer_log(1, 0, 0, "0xfailed"));
        require(failed_start_deliveries == 0, "failed generation must reject late callbacks");

        failed_transport->start_result = true;
        require(indexer.start_live(), "indexer should permit a clean retry");
        failed_transport->emit_from(0, make_indexer_log(2, 0, 0, "0xstale"));
        failed_transport->emit(make_indexer_log(2, 0, 1, "0xcurrent"));
        require(wait_for_indexer([&]() { return failed_start_deliveries == 1; }),
                "retry must accept only the current subscription generation");
    }

    const auto path = std::filesystem::temp_directory_path() / "polymarket_cpp_client_cursor_test.json";
    std::filesystem::remove(path);
    {
        FileBlockCursorStore store(path.string());
        require(!store.load("ctf"), "empty cursor should not load");
        store.save("ctf", 12345);
        store.save("uma", 77);
        require(store.load("ctf").value() == 12345, "cursor save failed");
    }
    {
        FileBlockCursorStore store(path.string());
        require(store.load("ctf").value() == 12345, "cursor reload failed");
        require(store.load("uma").value() == 77, "second cursor reload failed");
        store.save("ctf", 12346);
        require(store.load("ctf").value() == 12346, "cursor update failed");
        store.save("ctf", 12);
        require(store.load("ctf").value() == 12346, "cursor regressed");
    }
    // An in-block cursor survives restart while its legacy view exposes only
    // the last fully processed block, keeping older callers replay-safe.
    {
        FileBlockCursorStore store(path.string());
        EvmIndexCursor partial;
        partial.block_number = 12347;
        partial.block_complete = false;
        partial.last_log = EvmLogPosition{2, 4, "0xabc"};
        store.save_cursor("ctf", partial);
        const auto loaded = store.load_cursor("ctf");
        require(loaded.has_value(), "exact cursor should reload");
        require(loaded->block_number == 12347, "exact cursor block should reload");
        require(!loaded->block_complete, "partial block must not become complete");
        require(loaded->last_log.has_value(), "partial cursor must retain its log position");
        require(loaded->last_log->transaction_index == 2, "transaction index should reload");
        require(loaded->last_log->log_index == 4, "log index should reload");
        require(loaded->last_log->block_hash == "0xabc", "block hash should reload");
        require(store.load("ctf").value() == 12346,
                "legacy block cursor must expose only a completed block");
        store.rewind("ctf", 12340);
        const auto rewound = store.load_cursor("ctf");
        require(rewound->block_number == 12340, "rewind should move to the affected block");
        require(!rewound->block_complete, "rewind should replay the affected block");
        require(!rewound->last_log.has_value(), "rewind should replay from the block start");
        store.rewind("ctf", 12344);
        require(store.load_cursor("ctf")->block_number == 12340,
                "later removals must preserve the earliest pending rewind");
        store.rewind("missing", 50);
        require(!store.load_cursor("missing"),
                "removed unconfirmed log must not create a forward cursor");
    }

    // A callback failure between two logs in one block must resume after the
    // committed log, not skip the rest of that block.
    const auto resume_path = std::filesystem::temp_directory_path() / "polymarket_cpp_client_cursor_resume_test.json";
    std::filesystem::remove(resume_path);
    auto resume_store = std::make_shared<FileBlockCursorStore>(resume_path.string());
    auto resume_transport = std::make_shared<FakeEvmEventTransport>();
    resume_transport->head = "0xa";
    resume_transport->logs = {make_indexer_log(10, 0, 0, "0xfirst"),
                              make_indexer_log(10, 0, 1, "0xsecond")};

    EvmEventIndexerConfig resume_config;
    resume_config.start_block = 10;
    resume_config.batch_size = 100;
    resume_config.cursor_name = "resume";
    bool interrupted = false;
    {
        EvmEventIndexer indexer(resume_config, resume_store, resume_transport);
        int callbacks = 0;
        indexer.on_log([&callbacks](const EvmIndexedLog &)
                       {
                           if (++callbacks == 2)
                               throw std::runtime_error("simulated crash between logs");
                       });
        try
        {
            indexer.catch_up();
        }
        catch (const std::runtime_error &)
        {
            interrupted = true;
        }
    }
    require(interrupted, "first scan should stop at the simulated crash");
    auto partial = resume_store->load_cursor("resume");
    require(partial && !partial->block_complete, "first processed log needs an exact cursor");
    require(partial->last_log && partial->last_log->log_index == 0,
            "cursor must point at the completed first log");

    std::vector<std::string> replayed;
    {
        EvmEventIndexer indexer(resume_config, resume_store, resume_transport);
        indexer.on_log([&replayed](const EvmIndexedLog &item)
                       { replayed.push_back(item.log.transaction_hash); });
        indexer.catch_up();
    }
    require(replayed.size() == 1 && replayed.front() == "0xsecond",
            "restart should resume with the uncommitted second log");
    const auto completed = resume_store->load_cursor("resume");
    require(completed && completed->block_complete && completed->block_number == 10,
            "successful restart should complete the block cursor");

    // Logs arriving while HTTP scans the captured head are buffered, then
    // released without duplicating the confirmation-window overlap.
    const auto handoff_path = std::filesystem::temp_directory_path() / "polymarket_cpp_client_cursor_handoff_test.json";
    std::filesystem::remove(handoff_path);
    auto handoff_store = std::make_shared<FileBlockCursorStore>(handoff_path.string());
    handoff_store->save("handoff", 99);
    auto handoff_transport = std::make_shared<FakeEvmEventTransport>();
    handoff_transport->head = evm_uint64_to_quantity(105);
    handoff_transport->require_subscription_before_head = true;
    const auto overlap = make_indexer_log(105, 0, 0, "0xoverlap");
    const auto after_boundary = make_indexer_log(106, 0, 0, "0xlive");
    handoff_transport->logs = {make_indexer_log(100, 0, 0, "0xconfirmed"), overlap};
    bool injected_live = false;
    handoff_transport->during_get_logs = [&](const EvmLogFilter &)
    {
        if (injected_live)
            return;
        injected_live = true;
        handoff_transport->emit(overlap);
        handoff_transport->emit(after_boundary);
    };

    EvmEventIndexerConfig handoff_config;
    handoff_config.start_block = 1;
    handoff_config.confirmations = 2;
    handoff_config.batch_size = 50;
    handoff_config.cursor_name = "handoff";
    std::vector<std::pair<std::string, bool>> handed_off;
    std::atomic<std::size_t> handed_off_count{0};
    {
        EvmEventIndexer indexer(handoff_config, handoff_store, handoff_transport);
        indexer.on_log([&](const EvmIndexedLog &item)
                       {
                           handed_off.emplace_back(item.log.transaction_hash, item.live);
                           handed_off_count = handed_off.size();
                       });
        require(indexer.start_live(), "subscribe-first handoff should start");
        const auto initial_cursor = handoff_store->load_cursor("handoff");
        require(initial_cursor && initial_cursor->block_complete &&
                    initial_cursor->block_number == 103,
                "durable handoff cursor must stop at the confirmed boundary");

        handoff_transport->head = evm_uint64_to_quantity(108);
        handoff_transport->emit_head(108);
        require(wait_for_cursor(handoff_store, "handoff", 106, true),
                "later live head reconciliation should finish asynchronously");
        const auto advanced_cursor = handoff_store->load_cursor("handoff");
        require(advanced_cursor && advanced_cursor->block_complete &&
                    advanced_cursor->block_number == 106,
                "later live head must reconcile the newly confirmed range");
        handoff_transport->emit(make_indexer_log(108, 0, 0, "0xlater"));

        for (uint64_t block = 109; block <= 175; ++block)
            handoff_transport->emit(make_indexer_log(block, 0, 0, "0x" + std::to_string(block)));
        handoff_transport->emit_head(175);
        require(wait_for_cursor(handoff_store, "handoff", 173, true),
                "dedup pruning head should finish asynchronously");
        require(wait_for_indexer([&]() { return handed_off_count.load() == 71; }),
                "queued live callbacks should drain after reconciliation");
        const auto before_replay = handed_off_count.load();
        handoff_transport->emit(make_indexer_log(108, 0, 0, "0xlater"));
        handoff_transport->emit(make_indexer_log(175, 0, 0, "0x175"));
        require(wait_for_indexer([&]()
                                 { return handed_off_count.load() == before_replay + 1; }),
                "dedup cache should prune durable history but retain its recent window");
    }
    require(handed_off[0] == std::make_pair(std::string("0xconfirmed"), false),
            "confirmed catch-up log should be first");
    require(handed_off[1] == std::make_pair(std::string("0xoverlap"), false),
            "confirmation-window overlap should come from reconciliation once");
    require(handed_off[2] == std::make_pair(std::string("0xlive"), true),
            "post-boundary buffered log should be released as live");
    require(handed_off[3] == std::make_pair(std::string("0xlater"), true),
            "new unconfirmed log should remain live while advancing older checkpoints");

    // A removed live log invalidates the durable suffix starting at its block.
    const auto reorg_path = std::filesystem::temp_directory_path() / "polymarket_cpp_client_cursor_reorg_test.json";
    std::filesystem::remove(reorg_path);
    auto reorg_store = std::make_shared<FileBlockCursorStore>(reorg_path.string());
    reorg_store->save("reorg", 20);
    auto reorg_transport = std::make_shared<FakeEvmEventTransport>();
    reorg_transport->head = evm_uint64_to_quantity(20);
    const auto replacement = make_indexer_log(18, 0, 0, "0xreplacement");
    const auto suffix = make_indexer_log(19, 0, 0, "0xsuffix");
    const auto reorg_later = make_indexer_log(21, 0, 0, "0xlater-reorg");
    reorg_transport->logs = {replacement, suffix, reorg_later};
    EvmEventIndexerConfig reorg_config;
    reorg_config.cursor_name = "reorg";
    auto removed_log = make_indexer_log(18, 0, 0, "0xreorged");
    removed_log.removed = true;
    bool removed_delivered = false;
    bool replacement_delivered = false;
    bool suffix_delivered = false;
    {
        EvmEventIndexer indexer(reorg_config, reorg_store, reorg_transport);
        indexer.on_log([&](const EvmIndexedLog &item)
                       {
                           removed_delivered |= item.log.removed;
                           replacement_delivered |= item.log.transaction_hash == "0xreplacement";
                           suffix_delivered |= item.log.transaction_hash == "0xsuffix";
                       });
        require(indexer.start_live(), "reorg test live handoff should start");
        reorg_transport->emit(removed_log);
        require(wait_for_cursor(reorg_store, "reorg", 18, false),
                "removed log rewind should finish on the delivery worker");
        const auto rewound = reorg_store->load_cursor("reorg");
        require(rewound && rewound->block_number == 18 && !rewound->block_complete &&
                    !rewound->last_log,
                "removed log must rewind persistence to the affected block start");
        reorg_transport->emit(reorg_later);
        const auto still_rewound = reorg_store->load_cursor("reorg");
        require(still_rewound && still_rewound->block_number == 18 &&
                    !still_rewound->block_complete,
                "ordinary live logs must not advance past an unreplayed reorg suffix");
        reorg_transport->head = evm_uint64_to_quantity(21);
        reorg_transport->emit_head(21);
        require(wait_for_cursor(reorg_store, "reorg", 21, true),
                "reorg suffix reconciliation should finish asynchronously");
        const auto restored = reorg_store->load_cursor("reorg");
        require(restored && restored->block_number == 21 && restored->block_complete,
                "head reconciliation must complete the canonical replacement suffix");
    }
    require(removed_delivered, "removed log must still reach the state projector");
    require(replacement_delivered && suffix_delivered,
            "reorg reconciliation must deliver replacement and canonical suffix logs");

    run_evm_indexer_adversarial_tests();
    run_evm_indexer_delivery_tests();
    run_handoff_buffer_overflow_test();
    run_same_path_cursor_store_test();
    run_delivery_error_flood_test();

    std::filesystem::remove(path);
    std::filesystem::remove(resume_path);
    std::filesystem::remove(handoff_path);
    std::filesystem::remove(reorg_path);
    std::cout << "test_evm_event_indexer passed\n";
    return 0;
}
