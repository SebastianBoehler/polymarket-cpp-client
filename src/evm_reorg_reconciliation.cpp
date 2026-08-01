#include "evm_indexer_transport.hpp"
#include "evm_log_identity.hpp"
#include <algorithm>
#include <unordered_set>

namespace polymarket
{
    void detail::EvmEventIndexerImpl::report_reconciliation_failure(
        uint64_t generation, bool recover_overlap,
        std::string_view message) noexcept
    {
        try
        {
            bool active = false;
            {
                std::lock_guard<std::mutex> lock(live_state_->mutex);
                active = live_state_->accepting_live &&
                         generation == live_state_->generation;
                if (active && recover_overlap)
                    live_state_->overlap_recovery_pending = true;
            }
            if (active)
                emit_error(message);
        }
        catch (...)
        {
        }
    }

    void detail::EvmEventIndexerImpl::schedule_overlap_recovery(uint64_t generation)
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
                live_state_->overlap_recovery_pending = true;
                live_state_->pending_generation = generation;
                scheduled = true;
            }
        }
        if (scheduled)
            live_state_->reconcile_cv.notify_one();
    }

    void detail::EvmEventIndexerImpl::reconcile_gap(uint64_t target,
                                                    uint64_t durable_through,
                                                    uint64_t generation)
    {
        const auto overlap = config_.reorg_lookback_blocks - 1;
        const auto window_start = target > overlap ? target - overlap : 0;
        const auto from = std::max(config_.start_block, window_start);
        if (from > target)
            return;

        if (cursor_store_)
        {
            try
            {
                cursor_store_->rewind(config_.cursor_name, from);
            }
            catch (const std::exception &error)
            {
                emit_error(std::string("failed to persist EVM gap rewind: ") +
                           error.what());
            }
        }

        const auto durable_to = std::min(target, durable_through);
        if (!scan_gap_ranges(from, durable_to, true, generation))
            return;
        if (target > durable_through)
            scan_gap_ranges(std::max(from, durable_through + 1), target,
                            false, generation);
    }

    bool detail::EvmEventIndexerImpl::scan_gap_ranges(uint64_t from, uint64_t to,
                                                      bool persist,
                                                      uint64_t generation)
    {
        for (const auto &range : evm_make_block_ranges(from, to, config_.batch_size))
        {
            if (!generation_current(generation))
                return false;
            auto filter = config_.filter;
            filter.from_block = evm_uint64_to_quantity(range.from_block);
            filter.to_block = evm_uint64_to_quantity(range.to_block);
            auto canonical = transport_->get_logs(filter);
            if (!generation_current(generation))
                return false;
            std::stable_sort(canonical.begin(), canonical.end(),
                             detail::evm_log_before);

            std::unordered_set<std::string> canonical_ids;
            for (const auto &log : canonical)
            {
                if (!log.removed)
                    canonical_ids.insert(detail::evm_log_identity(log));
            }

            std::vector<EvmLog> orphans;
            {
                std::lock_guard<std::mutex> lock(live_state_->mutex);
                for (const auto &[identity, delivered] : live_state_->delivered_logs)
                {
                    if (delivered.block >= range.from_block &&
                        delivered.block <= range.to_block &&
                        !canonical_ids.contains(identity))
                    {
                        auto orphan = delivered.log;
                        orphan.removed = true;
                        orphans.push_back(std::move(orphan));
                    }
                }
            }
            std::stable_sort(orphans.begin(), orphans.end(),
                             detail::evm_log_before);
            for (const auto &orphan : orphans)
            {
                if (!dispatch_log(orphan, false, generation, true))
                    return false;
            }
            for (const auto &log : canonical)
            {
                if (!generation_current(generation))
                    return false;
                dispatch_log(log, false, generation, true);
                if (!generation_current(generation))
                    return false;
                if (persist && !log.removed)
                    save_log_checkpoint(log, false);
            }
            if (!generation_current(generation))
                return false;
            if (persist)
                save_checkpoint(range.to_block);
        }
        return true;
    }
}
