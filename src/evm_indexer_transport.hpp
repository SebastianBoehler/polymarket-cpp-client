#pragma once

#include "evm_indexer_impl.hpp"
#include <cstddef>
#include <condition_variable>
#include <deque>
#include <exception>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace polymarket
{
    inline constexpr std::size_t evm_live_log_queue_limit = 1024;
    inline constexpr std::size_t evm_delivery_error_limit = 64;

    struct EvmDeliveryCompletion
    {
        std::mutex mutex;
        std::condition_variable cv;
        bool done{false};
        bool delivered{false};
        std::exception_ptr failure;
    };

    struct EvmDeliveryJob
    {
        EvmLog log;
        bool live{false};
        uint64_t generation{0};
        std::shared_ptr<EvmDeliveryCompletion> completion;
    };

    struct EvmDeliveryError
    {
        uint64_t generation{0};
        std::string message;
        uint64_t occurrences{1};
    };

    struct EvmDeliveredLog
    {
        EvmLog log;
        uint64_t block{0};
    };

}

namespace polymarket::detail
{
    struct EvmEventIndexerImpl::LiveState
    {
        std::mutex mutex;
        std::mutex reconcile_mutex;
        std::condition_variable reconcile_cv;
        std::condition_variable delivery_cv;
        std::thread reconcile_thread;
        std::thread delivery_thread;
        std::thread::id reconcile_worker_id;
        std::thread::id delivery_worker_id;
        bool buffering{false};
        bool started{false};
        bool accepting_live{false};
        bool has_buffered_head{false};
        bool has_pending_head{false};
        bool refresh_head_pending{false};
        bool overlap_recovery_pending{false};
        bool handoff_gap_pending{false};
        bool worker_shutdown{false};
        bool delivery_shutdown{false};
        bool reconciliation_active{false};
        uint64_t generation{0};
        uint64_t durable_through{0};
        uint64_t buffered_head{0};
        uint64_t pending_head{0};
        uint64_t pending_generation{0};
        std::vector<EvmLog> buffered_logs;
        std::deque<EvmDeliveryJob> historical_deliveries;
        std::deque<EvmDeliveryJob> live_deliveries;
        std::deque<EvmDeliveryError> delivery_errors;
        uint64_t coalesced_delivery_errors{0};
        uint64_t delivery_error_generation{0};
        std::unordered_map<std::string, EvmDeliveredLog> delivered_logs;

        void queue_delivery_error_locked(uint64_t error_generation, std::string message)
        {
            if (delivery_error_generation != error_generation)
            {
                delivery_errors.clear();
                coalesced_delivery_errors = 0;
                delivery_error_generation = error_generation;
            }
            if (!delivery_errors.empty() &&
                delivery_errors.back().message == message)
            {
                ++delivery_errors.back().occurrences;
                return;
            }
            if (delivery_errors.size() >= evm_delivery_error_limit)
            {
                coalesced_delivery_errors += delivery_errors.front().occurrences;
                delivery_errors.pop_front();
            }
            delivery_errors.push_back({error_generation, std::move(message), 1});
        }
    };

}

namespace polymarket
{
    std::shared_ptr<EvmEventIndexerTransport>
    make_json_rpc_indexer_transport(const EvmEventIndexerConfig &config);
}
