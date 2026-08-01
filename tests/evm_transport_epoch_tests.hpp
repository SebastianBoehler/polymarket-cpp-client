#pragma once

#include "../src/evm_transport_epoch.hpp"
#include "evm_indexer_adversarial_tests.hpp"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

inline void run_evm_transport_epoch_test()
{
    polymarket::detail::EvmTransportEpoch epoch;
    epoch.advance(1, [] {});
    std::mutex gate_mutex;
    std::condition_variable gate_cv;
    bool message_entered = false;
    bool release_message = false;
    bool message_finished = false;
    bool message_admitted = false;
    std::thread message([&]
                        {
                            message_admitted = epoch.admit(1, [&]
                                                           {
                                                               std::unique_lock lock(gate_mutex);
                                                               message_entered = true;
                                                               gate_cv.notify_all();
                                                               gate_cv.wait(lock, [&]
                                                                            { return release_message; });
                                                               message_finished = true;
                                                           });
                        });
    {
        std::unique_lock lock(gate_mutex);
        evm_adversarial_require(
            gate_cv.wait_for(lock, std::chrono::seconds(1),
                             [&] { return message_entered; }),
            "old-epoch message should enter admission");
    }

    std::atomic<bool> advance_started{false};
    std::atomic<bool> gap_finished{false};
    bool gap_saw_finished_message = false;
    std::thread gap([&]
                    {
                        advance_started = true;
                        epoch.advance(2, [&]
                                      {
                                          gap_saw_finished_message = message_finished;
                                          gap_finished = true;
                                      });
                    });
    evm_adversarial_require(wait_for_indexer([&] { return advance_started.load(); }),
                            "gap invalidation thread should start");
    evm_adversarial_require(
        !wait_for_indexer([&] { return gap_finished.load(); },
                          std::chrono::milliseconds(30)),
        "gap invalidation must serialize behind in-flight message admission");
    {
        std::lock_guard lock(gate_mutex);
        release_message = true;
    }
    gate_cv.notify_all();
    message.join();
    gap.join();

    evm_adversarial_require(message_admitted && gap_saw_finished_message,
                            "accepted old message must finish before gap notification");
    evm_adversarial_require(!epoch.admit(1, [] {}),
                            "old epoch must be rejected after gap invalidation");
    evm_adversarial_require(epoch.admit(2, [] {}),
                            "current epoch must remain admissible");
}
