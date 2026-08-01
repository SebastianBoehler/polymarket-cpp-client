#include "oracle_watcher.hpp"
#include "evm_event_indexer.hpp"
#include <algorithm>
#include <tuple>

namespace polymarket
{
    namespace
    {
        uint64_t quantity_or_zero(const std::string &value)
        {
            if (value.empty())
                return 0;
            try
            {
                return evm_quantity_to_uint64(value);
            }
            catch (...)
            {
                return 0;
            }
        }

        bool canonical_before(const EvmLog &left, const EvmLog &right)
        {
            return std::make_tuple(quantity_or_zero(left.block_number),
                                   quantity_or_zero(left.transaction_index),
                                   quantity_or_zero(left.log_index),
                                   left.block_hash, left.transaction_hash) <
                   std::make_tuple(quantity_or_zero(right.block_number),
                                   quantity_or_zero(right.transaction_index),
                                   quantity_or_zero(right.log_index),
                                   right.block_hash, right.transaction_hash);
        }
    } // namespace

    void OracleResolutionDashboard::ingest_event(const PolymarketDecodedEvent &event,
                                                  bool live, uint64_t received_at_ms)
    {
        const auto key = state_key(event);
        if (key.empty())
            return;

        auto &history = histories_[key];
        if (event.raw_log.removed)
        {
            const auto existing = std::find_if(
                history.begin(), history.end(), [this, &event](const StoredEvent &stored)
                { return same_log(stored.event.raw_log, event.raw_log); });
            if (existing == history.end())
                return;
            history.erase(existing);
            rebuild_state(key);
            return;
        }

        const auto replay = std::find_if(
            history.begin(), history.end(), [this, &event](const StoredEvent &stored)
            { return same_log(stored.event.raw_log, event.raw_log); });
        if (replay != history.end())
            return;

        const auto compacted = compacted_through_.find(key);
        if (compacted != compacted_through_.end() &&
            !canonical_before(compacted->second.event.raw_log, event.raw_log))
            return;

        StoredEvent stored{event, live, received_at_ms};
        const auto insertion = std::lower_bound(
            history.begin(), history.end(), stored,
            [](const StoredEvent &left, const StoredEvent &right)
            { return canonical_before(left.event.raw_log, right.event.raw_log); });
        history.insert(insertion, std::move(stored));
        compact_history(key);
        rebuild_state(key);
    }

    void OracleResolutionDashboard::compact_history(const std::string &key)
    {
        auto &history = histories_[key];
        auto &base = compacted_states_[key];
        base.key = key;
        while (history.size() > retained_history_events_per_key)
        {
            auto oldest = std::move(history.front());
            history.erase(history.begin());
            touch_ids(base, oldest.event);
            append_event(base, oldest.event, oldest.live, oldest.received_at_ms);
            base.timeline.clear();
            compacted_through_[key] = oldest;
        }
    }

    void OracleResolutionDashboard::rebuild_state(const std::string &key)
    {
        const auto history_it = histories_.find(key);
        const auto base_it = compacted_states_.find(key);
        const bool no_history = history_it == histories_.end() || history_it->second.empty();
        const bool no_base = base_it == compacted_states_.end() || base_it->second.events_seen == 0;
        if (no_history && no_base)
        {
            states_.erase(key);
            return;
        }

        OracleResolutionState rebuilt = no_base ? OracleResolutionState{} : base_it->second;
        rebuilt.key = key;
        if (!no_history)
        {
            for (const auto &stored : history_it->second)
            {
                touch_ids(rebuilt, stored.event);
                append_event(rebuilt, stored.event, stored.live, stored.received_at_ms);
            }
        }
        states_[key] = std::move(rebuilt);
    }

    bool OracleResolutionDashboard::same_log(const EvmLog &left, const EvmLog &right) const
    {
        return !left.transaction_hash.empty() && !left.log_index.empty() &&
               left.block_hash == right.block_hash &&
               left.transaction_hash == right.transaction_hash &&
               left.log_index == right.log_index;
    }
} // namespace polymarket
