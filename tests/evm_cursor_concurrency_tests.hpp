#pragma once

#include "evm_indexer_adversarial_tests.hpp"
#include <atomic>
#include <barrier>
#include <filesystem>
#include <string>
#include <thread>

inline void run_same_path_cursor_store_test()
{
    using namespace polymarket;
    const auto directory = std::filesystem::temp_directory_path() /
                           "polymarket_cpp_client_cursor_concurrency";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    const auto path = directory / "state.json";
    const auto alias = directory / "." / "state.json";
    FileBlockCursorStore left(path.string());
    FileBlockCursorStore right(alias.string());

    constexpr uint64_t rounds = 64;
    std::barrier start_round(2);
    std::atomic<uint64_t> write_failures{0};
    const auto writer = [&](FileBlockCursorStore &store, const std::string &prefix,
                            uint64_t value_base)
    {
        for (uint64_t round = 0; round < rounds; ++round)
        {
            start_round.arrive_and_wait();
            try
            {
                store.save(prefix + std::to_string(round), value_base + round);
            }
            catch (const std::exception &)
            {
                ++write_failures;
            }
        }
    };

    std::thread left_writer(writer, std::ref(left), "left-", 1);
    std::thread right_writer(writer, std::ref(right), "right-", 1001);
    left_writer.join();
    right_writer.join();
    evm_adversarial_require(write_failures.load() == 0,
                            "same-path cursor instances must not collide on temporary files");

    FileBlockCursorStore reader(path.string());
    bool complete = true;
    try
    {
        for (uint64_t round = 0; round < rounds; ++round)
        {
            complete &= reader.load("left-" + std::to_string(round)) ==
                        std::optional<uint64_t>(1 + round);
            complete &= reader.load("right-" + std::to_string(round)) ==
                        std::optional<uint64_t>(1001 + round);
        }
    }
    catch (const std::exception &)
    {
        complete = false;
    }
    evm_adversarial_require(complete,
                            "same-path cursor writes must retain every independent cursor");
    std::filesystem::remove_all(directory);
}
