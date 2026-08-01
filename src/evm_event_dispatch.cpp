#include "evm_indexer_transport.hpp"
#include "evm_log_identity.hpp"
#include <chrono>
#include <optional>
#include <stdexcept>

namespace polymarket
{
    namespace
    {
        uint64_t received_at_ms()
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                .count();
        }

        void finish_delivery(const std::shared_ptr<EvmDeliveryCompletion> &completion,
                             bool delivered, std::exception_ptr failure = nullptr)
        {
            if (!completion)
                return;
            {
                std::lock_guard<std::mutex> lock(completion->mutex);
                completion->delivered = delivered;
                completion->failure = std::move(failure);
                completion->done = true;
            }
            completion->cv.notify_all();
        }
    } // namespace
    bool detail::EvmEventIndexerImpl::generation_current(uint64_t generation) const
    {
        std::lock_guard<std::mutex> lock(live_state_->mutex);
        return generation == live_state_->generation;
    }
    bool detail::EvmEventIndexerImpl::dispatch_log(const EvmLog &log, bool live,
                                                   uint64_t generation, bool wait)
    {
        if (wait)
        {
            bool on_delivery_thread = false;
            {
                std::lock_guard<std::mutex> lock(live_state_->mutex);
                on_delivery_thread = live_state_->delivery_worker_id ==
                                     std::this_thread::get_id();
            }
            if (on_delivery_thread)
                return generation_current(generation) && emit_log(log, live);
        }
        auto completion = wait ? std::make_shared<EvmDeliveryCompletion>() : nullptr;
        std::deque<EvmDeliveryJob> dropped;
        bool overflow = false;
        {
            std::lock_guard<std::mutex> lock(live_state_->mutex);
            if (live_state_->delivery_shutdown || generation != live_state_->generation)
                return false;
            if (live && live_state_->live_deliveries.size() >= evm_live_log_queue_limit)
            {
                dropped.swap(live_state_->live_deliveries);
                live_state_->queue_delivery_error_locked(
                    generation, "EVM event delivery queue overflow; HTTP reconciliation scheduled");
                live_state_->refresh_head_pending = true;
                live_state_->overlap_recovery_pending = true;
                live_state_->pending_generation = generation;
                overflow = true;
            }
            if (!overflow)
            {
                EvmDeliveryJob job{log, live, generation, completion};
                auto &queue = live ? live_state_->live_deliveries
                                   : live_state_->historical_deliveries;
                queue.push_back(std::move(job));
            }
        }
        for (const auto &job : dropped)
            finish_delivery(job.completion, false);
        if (overflow)
        {
            live_state_->reconcile_cv.notify_one();
            live_state_->delivery_cv.notify_one();
            return false;
        }
        live_state_->delivery_cv.notify_one();
        if (!completion)
            return true;

        std::unique_lock<std::mutex> lock(completion->mutex);
        completion->cv.wait(lock, [&]() { return completion->done; });
        if (completion->failure)
            std::rethrow_exception(completion->failure);
        return completion->delivered;
    }

    void detail::EvmEventIndexerImpl::delivery_loop()
    {
        for (;;)
        {
            EvmDeliveryJob job;
            std::optional<EvmDeliveryError> pending_error;
            bool current = false;
            {
                std::unique_lock<std::mutex> lock(live_state_->mutex);
                live_state_->delivery_cv.wait(lock, [this]()
                                              {
                                                  return live_state_->delivery_shutdown ||
                                                         !live_state_->historical_deliveries.empty() ||
                                                         !live_state_->delivery_errors.empty() ||
                                                         live_state_->coalesced_delivery_errors > 0 ||
                                                         (!live_state_->reconciliation_active &&
                                                          !live_state_->refresh_head_pending &&
                                                          !live_state_->live_deliveries.empty());
                                              });
                if (live_state_->delivery_shutdown)
                    return;
                if (!live_state_->historical_deliveries.empty())
                {
                    job = std::move(live_state_->historical_deliveries.front());
                    live_state_->historical_deliveries.pop_front();
                    current = job.generation == live_state_->generation;
                }
                else if (!live_state_->delivery_errors.empty())
                {
                    pending_error = std::move(live_state_->delivery_errors.front());
                    live_state_->delivery_errors.pop_front();
                    current = pending_error->generation == live_state_->generation;
                }
                else if (live_state_->coalesced_delivery_errors > 0)
                {
                    pending_error = EvmDeliveryError{
                        live_state_->delivery_error_generation,
                        "EVM delivery errors coalesced: " +
                            std::to_string(live_state_->coalesced_delivery_errors),
                        1};
                    live_state_->coalesced_delivery_errors = 0;
                    current = pending_error->generation == live_state_->generation;
                }
                else
                {
                    job = std::move(live_state_->live_deliveries.front());
                    live_state_->live_deliveries.pop_front();
                    current = job.generation == live_state_->generation;
                }
            }
            if (pending_error)
            {
                if (current)
                {
                    try
                    {
                        auto message = pending_error->message;
                        if (pending_error->occurrences > 1)
                            message += " (occurrences: " +
                                       std::to_string(pending_error->occurrences) + ')';
                        emit_error(message);
                    }
                    catch (...)
                    {
                    }
                }
                continue;
            }

            bool delivered = false;
            std::exception_ptr failure;
            if (current)
            {
                try
                {
                    delivered = emit_log(job.log, job.live);
                }
                catch (...)
                {
                    failure = std::current_exception();
                }
            }
            if (failure)
                schedule_overlap_recovery(job.generation);
            finish_delivery(job.completion, delivered, failure);
            if (failure && !job.completion && !delivery_shutdown_requested())
            {
                try
                {
                    std::rethrow_exception(failure);
                }
                catch (const std::exception &error)
                {
                    try
                    {
                        emit_error(error.what());
                    }
                    catch (...)
                    {
                    }
                }
                catch (...)
                {
                    try
                    {
                        emit_error("unknown EVM log callback failure");
                    }
                    catch (...)
                    {
                    }
                }
            }
        }
    }

    bool detail::EvmEventIndexerImpl::emit_log(const EvmLog &log, bool live)
    {
        std::optional<std::string> rewind_error;
        if (live && log.removed && !log.block_number.empty())
        {
            const auto block = evm_quantity_to_uint64(log.block_number);
            try
            {
                const auto cursor = cursor_store_
                                        ? cursor_store_->load_cursor(config_.cursor_name)
                                        : std::nullopt;
                if (cursor && block <= cursor->block_number)
                {
                    cursor_store_->rewind(config_.cursor_name, block);
                    if (auto callback = checkpoint_callback())
                        callback(block);
                }
            }
            catch (const std::exception &error)
            {
                rewind_error = std::string("failed to persist EVM reorg rewind: ") + error.what();
            }
        }
        if (delivery_shutdown_requested())
            return false;
        const auto id = detail::evm_log_identity(log);
        const auto block = detail::evm_log_block(log);
        std::optional<EvmDeliveredLog> removed_delivery;
        {
            std::lock_guard<std::mutex> lock(live_state_->mutex);
            if (log.removed)
            {
                const auto found = live_state_->delivered_logs.find(id);
                if (found != live_state_->delivered_logs.end())
                {
                    removed_delivery = std::move(found->second);
                    live_state_->delivered_logs.erase(found);
                }
            }
            else if (!id.empty() &&
                     !live_state_->delivered_logs.emplace(id, EvmDeliveredLog{log, block}).second)
                return false;
        }
        try
        {
            if (auto callback = log_callback())
                callback({log, live, received_at_ms()});
        }
        catch (...)
        {
            {
                std::lock_guard<std::mutex> lock(live_state_->mutex);
                if (removed_delivery)
                    live_state_->delivered_logs.emplace(id,
                                                        std::move(*removed_delivery));
                else if (!log.removed && !id.empty())
                    live_state_->delivered_logs.erase(id);
            }
            throw;
        }
        if (rewind_error && !delivery_shutdown_requested())
            emit_error(*rewind_error);
        return true;
    }

    void detail::EvmEventIndexerImpl::shutdown_delivery_worker(bool defer_join)
    {
        std::deque<EvmDeliveryJob> cancelled;
        {
            std::lock_guard<std::mutex> lock(live_state_->mutex);
            live_state_->delivery_shutdown = true;
            cancelled.swap(live_state_->historical_deliveries);
            cancelled.insert(cancelled.end(),
                             std::make_move_iterator(live_state_->live_deliveries.begin()),
                             std::make_move_iterator(live_state_->live_deliveries.end()));
            live_state_->live_deliveries.clear();
            live_state_->delivery_errors.clear();
            live_state_->coalesced_delivery_errors = 0;
        }
        for (const auto &job : cancelled)
            finish_delivery(job.completion, false);
        live_state_->delivery_cv.notify_all();
        if (live_state_->delivery_thread.joinable())
        {
            if (defer_join)
                live_state_->delivery_thread.detach();
            else
                live_state_->delivery_thread.join();
        }
    }
} // namespace polymarket
