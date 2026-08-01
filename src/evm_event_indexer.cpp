#include "evm_event_indexer.hpp"
#include "evm_indexer_transport.hpp"
#include "evm_log_identity.hpp"
#include <algorithm>
#include <stdexcept>
#include <thread>

namespace polymarket
{
    EvmCatchUpReport detail::EvmEventIndexerImpl::catch_up()
    {
        uint64_t latest = evm_quantity_to_uint64(transport_->block_number());
        uint64_t target = latest > config_.confirmations ? latest - config_.confirmations : 0;
        uint64_t generation = 0;
        {
            std::lock_guard<std::mutex> lock(live_state_->mutex);
            generation = live_state_->generation;
        }
        return catch_up_to(target, target, generation);
    }

    EvmCatchUpReport detail::EvmEventIndexerImpl::catch_up_to(uint64_t target,
                                                              uint64_t durable_through,
                                                              uint64_t generation)
    {
        auto saved = cursor_store_ ? cursor_store_->load_cursor(config_.cursor_name) : std::nullopt;
        if (saved && saved->block_complete && saved->block_number == static_cast<uint64_t>(-1))
            return {saved->block_number, target, 0, 0};
        uint64_t from = saved ? saved->block_number + (saved->block_complete ? 1 : 0)
                              : config_.start_block;

        EvmCatchUpReport report{from, target, 0, 0};
        const auto durable_to = std::min(target, durable_through);
        if (!scan_ranges(from, durable_to, true, saved, report, generation))
            return report;
        if (target > durable_through)
            scan_ranges(std::max(from, durable_through + 1), target, false,
                        saved, report, generation);
        return report;
    }

    bool detail::EvmEventIndexerImpl::scan_ranges(
        uint64_t from, uint64_t to, bool persist,
        const std::optional<EvmIndexCursor> &resume,
        EvmCatchUpReport &report, uint64_t generation)
    {
        for (const auto &range : evm_make_block_ranges(from, to, config_.batch_size))
        {
            if (!generation_current(generation))
                return false;
            auto filter = config_.filter;
            filter.from_block = evm_uint64_to_quantity(range.from_block);
            filter.to_block = evm_uint64_to_quantity(range.to_block);
            auto logs = transport_->get_logs(filter);
            if (!generation_current(generation))
                return false;
            std::stable_sort(logs.begin(), logs.end(), [](const EvmLog &left, const EvmLog &right)
                             {
                                 const auto left_block = evm_quantity_to_uint64(left.block_number);
                                 const auto right_block = evm_quantity_to_uint64(right.block_number);
                                 if (left_block != right_block)
                                     return left_block < right_block;
                                 const auto left_position = detail::evm_log_position(left);
                                 const auto right_position = detail::evm_log_position(right);
                                 return !detail::evm_position_at_or_before(right_position,
                                                                           left_position);
                             });
            for (const auto &log : logs)
            {
                if (!generation_current(generation))
                    return false;
                const auto block = evm_quantity_to_uint64(log.block_number);
                if (resume && !resume->block_complete && resume->last_log &&
                    block == resume->block_number &&
                    resume->last_log->block_hash == log.block_hash &&
                    detail::evm_position_at_or_before(detail::evm_log_position(log),
                                                      *resume->last_log))
                    continue;
                report.logs_seen++;
                dispatch_log(log, false, generation, true);
                if (!generation_current(generation))
                    return false;
                if (persist && !log.removed)
                    save_log_checkpoint(log, false);
            }
            report.ranges_scanned++;
            if (!generation_current(generation))
                return false;
            if (persist)
                save_checkpoint(range.to_block);
        }
        return true;
    }

    bool detail::EvmEventIndexerImpl::start_live()
    {
        uint64_t generation = 0;
        {
            std::lock_guard<std::mutex> lock(live_state_->mutex);
            if (live_state_->started)
                return true;
            generation = ++live_state_->generation;
            live_state_->accepting_live = true;
            live_state_->buffering = true;
            live_state_->buffered_logs.clear();
            live_state_->has_buffered_head = false;
            live_state_->refresh_head_pending = false;
            live_state_->overlap_recovery_pending = false;
            live_state_->handoff_gap_pending = false;
            live_state_->buffered_head = 0;
        }
        auto live_filter = config_.filter;
        live_filter.from_block.clear();
        live_filter.to_block.clear();
        const auto weak = weak_from_this();
        if (!transport_->start_logs(
            live_filter,
            [weak, generation](const EvmLog &log)
            {
                if (const auto self = weak.lock())
                    self->handle_live_log(log, generation);
            },
            [weak, generation](uint64_t block)
            {
                if (const auto self = weak.lock())
                    self->handle_live_head(block, generation);
            },
            [weak, generation](const std::string &error)
            {
                if (const auto self = weak.lock())
                    self->handle_live_error(error, generation);
            }))
        {
            transport_->stop();
            std::lock_guard<std::mutex> lock(live_state_->mutex);
            live_state_->accepting_live = false;
            live_state_->buffering = false;
            live_state_->started = false;
            live_state_->buffered_logs.clear();
            live_state_->has_buffered_head = false;
            live_state_->has_pending_head = false;
            live_state_->refresh_head_pending = false;
            live_state_->overlap_recovery_pending = false;
            live_state_->handoff_gap_pending = false;
            live_state_->buffered_head = 0;
            live_state_->pending_head = 0;
            return false;
        }

        try
        {
            const auto head = evm_quantity_to_uint64(transport_->block_number());
            const auto durable = head > config_.confirmations ? head - config_.confirmations : 0;
            {
                std::lock_guard<std::mutex> lock(live_state_->mutex);
                live_state_->durable_through = durable;
            }
            catch_up_to(head, durable, generation);
            if (!generation_current(generation))
                return false;
            drain_live_buffer(generation);
            std::lock_guard<std::mutex> lock(live_state_->mutex);
            if (!live_state_->accepting_live || generation != live_state_->generation)
                return false;
            live_state_->started = true;
            return true;
        }
        catch (const std::exception &error)
        {
            transport_->stop();
            {
                std::lock_guard<std::mutex> lock(live_state_->mutex);
                live_state_->accepting_live = false;
                live_state_->buffering = false;
                live_state_->started = false;
                live_state_->buffered_logs.clear();
                live_state_->has_buffered_head = false;
                live_state_->has_pending_head = false;
                live_state_->refresh_head_pending = false;
                live_state_->overlap_recovery_pending = false;
                live_state_->handoff_gap_pending = false;
                live_state_->buffered_head = 0;
                live_state_->pending_head = 0;
            }
            emit_error(error.what());
            return false;
        }
    }

    void detail::EvmEventIndexerImpl::run_live_for(std::chrono::seconds duration)
    {
        std::this_thread::sleep_for(duration);
        stop();
    }

    void detail::EvmEventIndexerImpl::stop()
    {
        bool called_from_worker = false;
        {
            std::lock_guard<std::mutex> lock(live_state_->mutex);
            const auto current = std::this_thread::get_id();
            called_from_worker = current == live_state_->delivery_worker_id ||
                                 current == live_state_->reconcile_worker_id;
            live_state_->accepting_live = false;
            ++live_state_->generation;
            live_state_->buffering = false;
            live_state_->started = false;
            live_state_->buffered_logs.clear();
            live_state_->has_buffered_head = false;
            live_state_->has_pending_head = false;
            live_state_->refresh_head_pending = false;
            live_state_->overlap_recovery_pending = false;
            live_state_->handoff_gap_pending = false;
            live_state_->buffered_head = 0;
            live_state_->pending_head = 0;
        }
        live_state_->reconcile_cv.notify_all();
        live_state_->delivery_cv.notify_all();
        transport_->stop();
        if (!called_from_worker)
        {
            std::lock_guard<std::mutex> reconcile_lock(live_state_->reconcile_mutex);
        }
    }

    void detail::EvmEventIndexerImpl::save_checkpoint(uint64_t block_number)
    {
        if (cursor_store_)
            cursor_store_->save_cursor(config_.cursor_name,
                                       {block_number, true, std::nullopt});
        if (auto callback = checkpoint_callback())
            callback(block_number);
        prune_delivered(block_number);
    }

    void detail::EvmEventIndexerImpl::save_log_checkpoint(const EvmLog &log, bool notify)
    {
        const auto block = evm_quantity_to_uint64(log.block_number);
        if (cursor_store_)
            cursor_store_->save_cursor(config_.cursor_name,
                                       {block, false, detail::evm_log_position(log)});
        if (notify)
        {
            if (auto callback = checkpoint_callback())
                callback(block);
        }
    }

} // namespace polymarket
