#pragma once

#include <algorithm>
#include <chrono>
#include <thread>

namespace polymarket::detail
{
    inline std::chrono::milliseconds remaining(std::chrono::steady_clock::time_point deadline)
    {
        return std::max(std::chrono::milliseconds::zero(),
                        std::chrono::ceil<std::chrono::milliseconds>(
                            deadline - std::chrono::steady_clock::now()));
    }

    // Sleeps until the next poll, never past the deadline, so a timeout
    // shorter than the poll interval still gets a final poll at the
    // deadline. Returns false once the deadline has passed.
    inline bool sleep_before_next_poll(std::chrono::steady_clock::time_point deadline,
                                       std::chrono::milliseconds poll_interval)
    {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(std::min(poll_interval, remaining(deadline)));
        return true;
    }
} // namespace polymarket::detail
