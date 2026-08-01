#pragma once

#include "evm_indexer_adversarial_tests.hpp"
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <thread>

inline void run_checkpoint_exception_test(bool throw_nonstandard)
{
    using namespace polymarket;
    const auto suffix = throw_nonstandard ? "nonstandard" : "standard";
    const auto path = std::filesystem::temp_directory_path() /
                      ("polymarket_cpp_client_checkpoint_" +
                       std::string(suffix) + ".json");
    std::filesystem::remove(path);
    auto store = std::make_shared<FileBlockCursorStore>(path.string());
    store->save(suffix, 10);
    auto transport = std::make_shared<FakeEvmEventTransport>();
    transport->head = "0xa";
    EvmEventIndexerConfig config;
    config.cursor_name = suffix;
    std::atomic<bool> failure_reported{false};
    std::atomic<bool> worker_recovered{false};
    {
        EvmEventIndexer indexer(config, store, transport);
        indexer.on_error([&](const std::string &error)
                         {
                             failure_reported = throw_nonstandard
                                                    ? error.find("unknown EVM reconciliation failure") != std::string::npos
                                                    : error.find("standard checkpoint failure") != std::string::npos;
                         });
        indexer.on_checkpoint([&](uint64_t block)
                              {
                                  if (block == 11)
                                  {
                                      if (throw_nonstandard)
                                          throw 7;
                                      throw std::runtime_error("standard checkpoint failure");
                                  }
                                  if (block == 12)
                                      worker_recovered = true;
                              });
        evm_adversarial_require(indexer.start_live(),
                                "checkpoint-exception indexer should start");
        transport->emit_head(11);
        evm_adversarial_require(wait_for_indexer([&] { return failure_reported.load(); }),
                                "checkpoint exception must reach on_error");
        transport->emit_head(12);
        evm_adversarial_require(wait_for_indexer([&] { return worker_recovered.load(); }),
                                "reconciliation worker must survive checkpoint exceptions");
    }
    std::filesystem::remove(path);
}

inline void run_callback_snapshot_race_test()
{
    using namespace polymarket;
    const auto path = std::filesystem::temp_directory_path() /
                      "polymarket_cpp_client_callback_snapshot_race.json";
    std::filesystem::remove(path);
    auto store = std::make_shared<FileBlockCursorStore>(path.string());
    store->save("callback-race", 10);
    auto transport = std::make_shared<FakeEvmEventTransport>();
    transport->head = "0xa";
    EvmEventIndexerConfig config;
    config.cursor_name = "callback-race";
    std::atomic<int> logs{0};
    std::atomic<int> checkpoints{0};
    std::atomic<int> errors{0};
    std::atomic<bool> setting{true};
    {
        EvmEventIndexer indexer(config, store, transport);
        indexer.on_log([&](const EvmIndexedLog &) { ++logs; });
        indexer.on_checkpoint([&](uint64_t) { ++checkpoints; });
        indexer.on_error([&](const std::string &) { ++errors; });
        evm_adversarial_require(indexer.start_live(),
                                "callback-race indexer should start");

        std::thread log_setter([&]
                               {
                                   while (setting.load())
                                       indexer.on_log([&](const EvmIndexedLog &) { ++logs; });
                               });
        std::thread checkpoint_setter([&]
                                      {
                                          while (setting.load())
                                              indexer.on_checkpoint([&](uint64_t) { ++checkpoints; });
                                      });
        std::thread error_setter([&]
                                 {
                                     while (setting.load())
                                         indexer.on_error([&](const std::string &) { ++errors; });
                                 });
        for (uint64_t index = 0; index < 256; ++index)
        {
            transport->emit(make_indexer_log(11, 0, index,
                                             "0xcallback-race-" + std::to_string(index)));
            transport->emit_error("callback snapshot race");
            transport->emit_head(11 + index);
        }
        evm_adversarial_require(wait_for_indexer([&]
                                                 {
                                                     return logs.load() > 0 &&
                                                            checkpoints.load() > 0 &&
                                                            errors.load() > 0;
                                                 }),
                                "all callback snapshots should remain callable while setters race");
        setting = false;
        log_setter.join();
        checkpoint_setter.join();
        error_setter.join();
    }
    std::filesystem::remove(path);
}

inline void run_evm_callback_safety_tests()
{
    const auto *focused = std::getenv("EVM_CHECKPOINT_EXCEPTION_CASE");
    if (!focused || std::string(focused) == "standard")
        run_checkpoint_exception_test(false);
    if (!focused || std::string(focused) == "nonstandard")
        run_checkpoint_exception_test(true);
    if (!focused || std::string(focused) == "race")
        run_callback_snapshot_race_test();
}
