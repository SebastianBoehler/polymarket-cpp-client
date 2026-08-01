#pragma once

#include <cstdint>
#include <mutex>
#include <utility>

namespace polymarket::detail
{
    class EvmTransportEpoch
    {
    public:
        template <typename Callback>
        bool admit(uint64_t generation, Callback &&callback)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (generation != generation_)
                return false;
            std::forward<Callback>(callback)();
            return true;
        }

        template <typename Callback>
        bool advance(uint64_t generation, Callback &&callback)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (generation <= generation_)
                return false;
            generation_ = generation;
            std::forward<Callback>(callback)();
            return true;
        }

    private:
        std::mutex mutex_;
        uint64_t generation_{0};
    };
}
