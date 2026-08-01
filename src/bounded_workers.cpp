#include "bounded_workers.hpp"

#include <algorithm>
#include <atomic>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace polymarket::detail
{
    void run_bounded_tasks(std::size_t task_count,
                           std::size_t worker_limit,
                           const BoundedTask &task)
    {
        if (task_count == 0)
        {
            return;
        }
        if (worker_limit == 0)
        {
            throw std::invalid_argument("worker limit must be positive");
        }

        const auto worker_count = std::min(task_count, worker_limit);
        std::atomic<std::size_t> next_task{0};
        std::atomic<bool> cancelled{false};
        std::mutex failure_mutex;
        std::exception_ptr failure;
        // A later thread construction can throw after earlier workers started.
        // jthread makes unwinding join those already-created workers instead of
        // destroying joinable std::thread objects and terminating the process.
        std::vector<std::jthread> workers;
        workers.reserve(worker_count);

        try
        {
            for (std::size_t worker = 0; worker < worker_count; ++worker)
            {
                workers.emplace_back([&, worker]
                {
                    while (!cancelled.load(std::memory_order_relaxed))
                    {
                        const auto index = next_task.fetch_add(1, std::memory_order_relaxed);
                        if (index >= task_count)
                        {
                            return;
                        }
                        try
                        {
                            task(index, worker);
                        }
                        catch (...)
                        {
                            {
                                std::lock_guard lock(failure_mutex);
                                if (!failure)
                                {
                                    failure = std::current_exception();
                                }
                            }
                            cancelled.store(true, std::memory_order_relaxed);
                        }
                    }
                });
            }
        }
        catch (...)
        {
            cancelled.store(true, std::memory_order_relaxed);
            throw;
        }
        for (auto &worker : workers)
        {
            worker.join();
        }
        if (failure)
        {
            std::rethrow_exception(failure);
        }
    }
}
