#pragma once

#include <cstddef>
#include <functional>

namespace polymarket::detail
{
    using BoundedTask = std::function<void(std::size_t task_index,
                                           std::size_t worker_index)>;

    void run_bounded_tasks(std::size_t task_count,
                           std::size_t worker_limit,
                           const BoundedTask &task);
}
