#pragma once

#include "evm_indexer_adversarial_tests.hpp"
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>

inline void run_log_callback_owner_reset_test()
{
    using namespace polymarket;
    auto store = std::make_shared<LegacyEvmCursorStore>();
    auto transport = std::make_shared<FakeEvmEventTransport>();
    transport->head = "0xa";
    EvmEventIndexerConfig config;
    config.cursor_name = "log-owner-reset";
    std::unique_ptr<EvmEventIndexer> owner =
        std::make_unique<EvmEventIndexer>(config, store, transport);
    std::atomic<bool> reset_returned{false};
    std::atomic<int> errors_after_reset{0};
    owner->on_error([&](const std::string &) { ++errors_after_reset; });
    owner->on_log([&](const EvmIndexedLog &)
                  {
                      owner.reset();
                      reset_returned = true;
                      throw std::runtime_error("log callback failed after owner reset");
                  });
    evm_adversarial_require(owner->start_live(),
                            "log-owner-reset indexer should start");
    auto removed = make_indexer_log(9, 0, 0, "0xreset-from-log");
    removed.removed = true;
    transport->emit(removed);
    evm_adversarial_require(wait_for_indexer([&] { return reset_returned.load(); }),
                            "owner reset from on_log must return without self-join or UAF");
    evm_adversarial_require(!owner, "on_log callback should release its owner");
    evm_adversarial_require(
        !wait_for_indexer([&] { return errors_after_reset.load() != 0; },
                          std::chrono::milliseconds(100)),
        "on_log owner reset must suppress later error callbacks");
}

inline void run_error_callback_owner_reset_test()
{
    using namespace polymarket;
    const auto path = std::filesystem::temp_directory_path() /
                      "polymarket_cpp_client_error_owner_reset.json";
    std::filesystem::remove(path);
    auto store = std::make_shared<FileBlockCursorStore>(path.string());
    store->save("error-owner-reset", 10);
    auto transport = std::make_shared<FakeEvmEventTransport>();
    transport->head = "0xa";
    EvmEventIndexerConfig config;
    config.cursor_name = "error-owner-reset";
    std::unique_ptr<EvmEventIndexer> owner =
        std::make_unique<EvmEventIndexer>(config, store, transport);
    std::atomic<bool> reset_returned{false};
    owner->on_error([&](const std::string &error)
                    {
                        if (error.find("reset owner from reconciliation") ==
                            std::string::npos)
                            return;
                        owner.reset();
                        reset_returned = true;
                    });
    evm_adversarial_require(owner->start_live(),
                            "error-owner-reset indexer should start");
    transport->head = "0xb";
    transport->during_get_logs = [](const EvmLogFilter &)
    {
        throw std::runtime_error("reset owner from reconciliation");
    };
    transport->emit_head(11);
    evm_adversarial_require(wait_for_indexer([&] { return reset_returned.load(); }),
                            "owner reset from reconcile on_error must return safely");
    evm_adversarial_require(!owner, "on_error callback should release its owner");
    std::filesystem::remove(path);
}

inline void run_checkpoint_callback_owner_reset_test()
{
    using namespace polymarket;
    const auto path = std::filesystem::temp_directory_path() /
                      "polymarket_cpp_client_checkpoint_owner_reset.json";
    std::filesystem::remove(path);
    auto store = std::make_shared<FileBlockCursorStore>(path.string());
    store->save("checkpoint-owner-reset", 10);
    auto transport = std::make_shared<FakeEvmEventTransport>();
    transport->head = "0xa";
    EvmEventIndexerConfig config;
    config.cursor_name = "checkpoint-owner-reset";
    std::unique_ptr<EvmEventIndexer> owner =
        std::make_unique<EvmEventIndexer>(config, store, transport);
    std::atomic<bool> reset_returned{false};
    std::atomic<int> logs_after_reset{0};
    owner->on_log([&](const EvmIndexedLog &) { ++logs_after_reset; });
    owner->on_checkpoint([&](uint64_t block)
                         {
                             if (block != 10)
                                 return;
                             owner.reset();
                             reset_returned = true;
                         });
    evm_adversarial_require(owner->start_live(),
                            "checkpoint-owner-reset indexer should start");
    auto removed = make_indexer_log(10, 0, 0, "0xreset-from-checkpoint");
    removed.removed = true;
    transport->emit(removed);
    evm_adversarial_require(wait_for_indexer([&] { return reset_returned.load(); }),
                            "owner reset from on_checkpoint must return safely");
    evm_adversarial_require(!owner, "on_checkpoint callback should release its owner");
    evm_adversarial_require(
        !wait_for_indexer([&] { return logs_after_reset.load() != 0; },
                          std::chrono::milliseconds(100)),
        "on_checkpoint owner reset must suppress the later log callback");
    std::filesystem::remove(path);
}

inline void run_evm_owner_reset_tests()
{
    const auto *focused = std::getenv("EVM_OWNER_RESET_CASE");
    if (!focused || std::string(focused) == "log")
        run_log_callback_owner_reset_test();
    if (!focused || std::string(focused) == "error")
        run_error_callback_owner_reset_test();
    if (!focused || std::string(focused) == "checkpoint")
        run_checkpoint_callback_owner_reset_test();
}
