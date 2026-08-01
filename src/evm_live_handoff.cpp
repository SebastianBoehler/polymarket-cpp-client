#include "evm_indexer_transport.hpp"
#include <algorithm>
#include <optional>

namespace polymarket
{
    void detail::EvmEventIndexerImpl::prune_delivered(uint64_t durable_block)
    {
        const auto retention_blocks = config_.reorg_lookback_blocks;
        if (durable_block <= retention_blocks)
            return;
        const auto cutoff = durable_block - retention_blocks;
        std::lock_guard<std::mutex> lock(live_state_->mutex);
        for (auto it = live_state_->delivered_logs.begin();
             it != live_state_->delivered_logs.end();)
        {
            if (it->second.block <= cutoff)
                it = live_state_->delivered_logs.erase(it);
            else
                ++it;
        }
    }

    void detail::EvmEventIndexerImpl::drain_live_buffer(uint64_t generation)
    {
        for (;;)
        {
            std::shared_ptr<EvmDeliveryCompletion> completion;
            std::optional<uint64_t> buffered_head;
            bool recover_gap = false;
            {
                std::lock_guard<std::mutex> lock(live_state_->mutex);
                if (live_state_->handoff_gap_pending)
                {
                    live_state_->handoff_gap_pending = false;
                    live_state_->buffered_logs.clear();
                    recover_gap = true;
                }
                else if (!live_state_->buffered_logs.empty())
                {
                    for (const auto &log : live_state_->buffered_logs)
                        live_state_->historical_deliveries.push_back(
                            {log, true, generation, nullptr});
                    completion = std::make_shared<EvmDeliveryCompletion>();
                    live_state_->historical_deliveries.back().completion = completion;
                    live_state_->buffered_logs.clear();
                }
                else
                {
                    live_state_->buffering = false;
                    if (live_state_->has_buffered_head)
                    {
                        buffered_head = live_state_->buffered_head;
                        live_state_->has_buffered_head = false;
                    }
                }
            }
            if (recover_gap)
            {
                const auto head = evm_quantity_to_uint64(transport_->block_number());
                const auto durable = head > config_.confirmations
                                         ? head - config_.confirmations
                                         : 0;
                reconcile_gap(head, durable, generation);
                std::lock_guard<std::mutex> lock(live_state_->mutex);
                if (generation == live_state_->generation)
                    live_state_->durable_through = durable;
                continue;
            }
            live_state_->delivery_cv.notify_all();
            if (completion)
            {
                std::unique_lock<std::mutex> lock(completion->mutex);
                completion->cv.wait(lock, [&]() { return completion->done; });
                if (completion->failure)
                    std::rethrow_exception(completion->failure);
                continue;
            }
            if (buffered_head)
                handle_live_head(*buffered_head, generation);
            return;
        }
    }

    void detail::EvmEventIndexerImpl::process_live_log(const EvmLog &log,
                                                       uint64_t generation,
                                                       bool wait)
    {
        dispatch_log(log, true, generation, wait);
    }

    void detail::EvmEventIndexerImpl::handle_live_head(uint64_t block_number,
                                                       uint64_t generation)
    {
        bool scheduled = false;
        {
            std::lock_guard<std::mutex> lock(live_state_->mutex);
            if (!live_state_->accepting_live || generation != live_state_->generation)
                return;
            if (live_state_->buffering)
            {
                live_state_->has_buffered_head = true;
                live_state_->buffered_head = std::max(live_state_->buffered_head, block_number);
                return;
            }
            live_state_->has_pending_head = true;
            live_state_->pending_head = std::max(live_state_->pending_head, block_number);
            live_state_->pending_generation = generation;
            scheduled = true;
        }
        if (scheduled)
            live_state_->reconcile_cv.notify_one();
    }

    void detail::EvmEventIndexerImpl::reconciliation_loop()
    {
        for (;;)
        {
            uint64_t head = 0;
            uint64_t generation = 0;
            bool refresh_head = false;
            bool recover_overlap = false;
            {
                std::unique_lock<std::mutex> lock(live_state_->mutex);
                live_state_->reconcile_cv.wait(lock, [this]()
                                               {
                                                   return live_state_->worker_shutdown ||
                                                          live_state_->has_pending_head ||
                                                          (live_state_->refresh_head_pending &&
                                                           !live_state_->buffering);
                                               });
                if (live_state_->worker_shutdown)
                    return;
                head = live_state_->pending_head;
                generation = live_state_->pending_generation;
                refresh_head = live_state_->refresh_head_pending;
                recover_overlap = live_state_->overlap_recovery_pending;
                live_state_->has_pending_head = false;
                live_state_->refresh_head_pending = false;
                live_state_->overlap_recovery_pending = false;
                live_state_->pending_head = 0;
                live_state_->reconciliation_active = true;
            }

            try
            {
                if (refresh_head)
                {
                    const auto latest = evm_quantity_to_uint64(transport_->block_number());
                    head = std::max(head, latest);
                }
                reconcile_confirmed(head, generation, refresh_head,
                                    recover_overlap);
            }
            catch (const std::exception &error)
            {
                report_reconciliation_failure(generation, recover_overlap,
                                              error.what());
            }
            catch (...)
            {
                report_reconciliation_failure(
                    generation, recover_overlap,
                    "unknown EVM reconciliation failure");
            }
            bool release_deliveries = false;
            {
                std::lock_guard<std::mutex> lock(live_state_->mutex);
                if (live_state_->worker_shutdown)
                {
                    live_state_->reconciliation_active = false;
                    release_deliveries = true;
                }
                else if (!live_state_->has_pending_head &&
                         !live_state_->refresh_head_pending &&
                         !live_state_->overlap_recovery_pending)
                {
                    live_state_->reconciliation_active = false;
                    release_deliveries = true;
                }
            }
            if (release_deliveries)
                live_state_->delivery_cv.notify_all();
            {
                std::lock_guard<std::mutex> lock(live_state_->mutex);
                if (live_state_->worker_shutdown)
                    return;
            }
        }
    }

    void detail::EvmEventIndexerImpl::reconcile_confirmed(
        uint64_t head, uint64_t generation, bool include_unconfirmed,
        bool recover_overlap)
    {
        std::lock_guard<std::mutex> reconcile_lock(live_state_->reconcile_mutex);
        {
            std::lock_guard<std::mutex> lock(live_state_->mutex);
            if (!live_state_->accepting_live || generation != live_state_->generation)
                return;
        }
        const auto durable = head > config_.confirmations ? head - config_.confirmations : 0;
        const auto target = include_unconfirmed ? head : durable;
        if (recover_overlap)
            reconcile_gap(target, durable, generation);
        else
            catch_up_to(target, durable, generation);
        std::lock_guard<std::mutex> lock(live_state_->mutex);
        if (live_state_->accepting_live && generation == live_state_->generation)
            live_state_->durable_through = durable;
    }

    void detail::EvmEventIndexerImpl::shutdown_reconciliation_worker(bool defer_join)
    {
        {
            std::lock_guard<std::mutex> lock(live_state_->mutex);
            live_state_->worker_shutdown = true;
            live_state_->has_pending_head = false;
            live_state_->refresh_head_pending = false;
            live_state_->overlap_recovery_pending = false;
        }
        live_state_->reconcile_cv.notify_all();
        if (live_state_->reconcile_thread.joinable())
        {
            if (defer_join)
                live_state_->reconcile_thread.detach();
            else
                live_state_->reconcile_thread.join();
        }
    }

    void detail::EvmEventIndexerImpl::handle_live_log(const EvmLog &log,
                                                      uint64_t generation)
    {
        try
        {
            bool buffered = false;
            bool overflow = false;
            {
                std::lock_guard<std::mutex> lock(live_state_->mutex);
                if (!live_state_->accepting_live || generation != live_state_->generation)
                    return;
                if (live_state_->buffering)
                {
                    buffered = true;
                    if (!live_state_->handoff_gap_pending &&
                        live_state_->buffered_logs.size() < evm_live_log_queue_limit)
                        live_state_->buffered_logs.push_back(log);
                    else if (!live_state_->handoff_gap_pending)
                    {
                        live_state_->buffered_logs.clear();
                        live_state_->handoff_gap_pending = true;
                        live_state_->queue_delivery_error_locked(
                            generation, "EVM handoff buffer overflow; full HTTP reconciliation scheduled");
                        overflow = true;
                    }
                }
            }
            if (overflow)
                live_state_->delivery_cv.notify_one();
            if (buffered)
                return;
            process_live_log(log, generation, false);
        }
        catch (const std::exception &error)
        {
            emit_error(error.what());
        }
    }

    void detail::EvmEventIndexerImpl::handle_live_error(const std::string &error,
                                                        uint64_t generation)
    {
        bool scheduled = false;
        {
            std::lock_guard<std::mutex> lock(live_state_->mutex);
            if (!live_state_->accepting_live || generation != live_state_->generation)
                return;
            if (live_state_->buffering)
            {
                live_state_->buffered_logs.clear();
                live_state_->handoff_gap_pending = true;
            }
            else
            {
                live_state_->refresh_head_pending = true;
                if (error.find("stream gap") != std::string::npos)
                    live_state_->overlap_recovery_pending = true;
            }
            live_state_->pending_generation = generation;
            live_state_->queue_delivery_error_locked(generation, error);
            scheduled = !live_state_->buffering;
        }
        if (scheduled)
            live_state_->reconcile_cv.notify_one();
        live_state_->delivery_cv.notify_one();
    }
} // namespace polymarket
