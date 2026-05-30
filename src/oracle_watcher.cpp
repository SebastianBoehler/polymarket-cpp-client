#include "oracle_watcher.hpp"
#include "evm_event_indexer.hpp"
#include <nlohmann/json.hpp>

namespace polymarket
{

    std::string oracle_resolution_phase_to_string(OracleResolutionPhase phase)
    {
        switch (phase)
        {
        case OracleResolutionPhase::Pending:
            return "PENDING";
        case OracleResolutionPhase::Proposed:
            return "PROPOSED";
        case OracleResolutionPhase::Disputed:
            return "DISPUTED";
        case OracleResolutionPhase::Resolved:
            return "RESOLVED";
        case OracleResolutionPhase::ManuallyResolved:
            return "MANUALLY_RESOLVED";
        default:
            return "UNKNOWN";
        }
    }

    nlohmann::json oracle_resolution_event_to_json(const OracleResolutionEvent &event)
    {
        nlohmann::json out = {
            {"phase", event.phase_name},
            {"event", event.event_name},
            {"live", event.live},
            {"receivedAtMs", event.received_at_ms},
            {"blockNumber", event.block_number},
            {"transactionHash", event.transaction_hash},
            {"removed", event.removed},
            {"questionId", event.question_id},
            {"conditionId", event.condition_id},
        };
        if (!event.settled_price.empty())
            out["settledPrice"] = event.settled_price;
        if (!event.payout.empty())
            out["payout"] = event.payout;
        if (!event.payouts.empty())
            out["payouts"] = event.payouts;
        return out;
    }

    nlohmann::json oracle_resolution_state_to_json(const OracleResolutionState &state)
    {
        nlohmann::json out = {
            {"key", state.key},
            {"questionId", state.question_id},
            {"conditionId", state.condition_id},
            {"phase", oracle_resolution_phase_to_string(state.phase)},
            {"eventsSeen", state.events_seen},
            {"disputeCount", state.dispute_count},
            {"lastSeenMs", state.last_seen_ms},
            {"lastEvent", state.last_event_name},
            {"lastTx", state.last_tx_hash},
            {"lastBlock", state.last_block},
            {"timeline", nlohmann::json::array()}};
        if (!state.settled_price.empty())
            out["settledPrice"] = state.settled_price;
        if (!state.payouts.empty())
            out["payouts"] = state.payouts;
        for (const auto &item : state.timeline)
        {
            out["timeline"].push_back(oracle_resolution_event_to_json(item));
        }
        return out;
    }

    std::string OracleResolutionDashboard::state_key(const PolymarketDecodedEvent &event)
    {
        if (!event.question_id.empty())
            return event.question_id;
        return event.condition_id;
    }

    OracleResolutionPhase OracleResolutionDashboard::next_phase(PolymarketEventKind kind) const
    {
        switch (kind)
        {
        case PolymarketEventKind::QuestionInitialized:
            return OracleResolutionPhase::Proposed;
        case PolymarketEventKind::QuestionFlagged:
        case PolymarketEventKind::QuestionReset:
            return OracleResolutionPhase::Disputed;
        case PolymarketEventKind::QuestionResolved:
        case PolymarketEventKind::ConditionResolution:
            return OracleResolutionPhase::Resolved;
        case PolymarketEventKind::QuestionManuallyResolved:
            return OracleResolutionPhase::ManuallyResolved;
        default:
            return OracleResolutionPhase::Unknown;
        }
    }

    void OracleResolutionDashboard::touch_ids(OracleResolutionState &state, const PolymarketDecodedEvent &event)
    {
        if (state.question_id.empty() && !event.question_id.empty())
            state.question_id = event.question_id;
        if (state.condition_id.empty() && !event.condition_id.empty())
            state.condition_id = event.condition_id;
    }

    std::string OracleResolutionDashboard::parse_block_number(const std::string &raw) const
    {
        if (raw.empty())
            return "";
        try
        {
            return std::to_string(evm_quantity_to_uint64(raw));
        }
        catch (...)
        {
            return raw;
        }
    }

    void OracleResolutionDashboard::append_event(OracleResolutionState &state,
                                                const PolymarketDecodedEvent &event, bool live, uint64_t received_at_ms)
    {
        auto next = next_phase(event.kind);
        if (next != OracleResolutionPhase::Unknown)
        {
            state.phase = next;
        }

        state.events_seen++;
        state.last_seen_ms = received_at_ms;
        state.last_tx_hash = event.raw_log.transaction_hash;
        state.last_event_name = event.name;
        state.last_block = 0;
        if (!event.raw_log.block_number.empty())
        {
            try
            {
                state.last_block = evm_quantity_to_uint64(event.raw_log.block_number);
            }
            catch (...)
            {
                state.last_block = 0;
            }
        }

        if (event.kind == PolymarketEventKind::QuestionFlagged ||
            event.kind == PolymarketEventKind::QuestionReset)
        {
            state.dispute_count++;
        }

        if (!event.settled_price.empty())
        {
            state.settled_price = event.settled_price;
        }
        if (!event.payout.empty())
        {
            state.payouts = {event.payout};
        }
        if (!event.payouts.empty())
        {
            state.payouts = event.payouts;
        }

        if (state.key.empty())
        {
            state.key = state_key(event);
        }

            state.timeline.push_back({
            {oracle_resolution_phase_to_string(state.phase)},
            event.name,
            live,
            received_at_ms,
            parse_block_number(event.raw_log.block_number),
            event.raw_log.transaction_hash,
            event.question_id,
            event.condition_id,
            event.settled_price,
            event.payouts,
            event.payout,
            event.raw_log.removed});
    }

    void OracleResolutionDashboard::ingest_event(const PolymarketDecodedEvent &event,
                                                bool live, uint64_t received_at_ms)
    {
        if (event.raw_log.removed)
            return;
        const auto key = state_key(event);
        if (key.empty())
            return;
        auto &state = states_[key];
        state.key = key;
        touch_ids(state, event);
        append_event(state, event, live, received_at_ms);
    }

    const OracleResolutionState *OracleResolutionDashboard::get(const std::string &key) const
    {
        auto it = states_.find(key);
        return it == states_.end() ? nullptr : &it->second;
    }

    std::vector<std::string> OracleResolutionDashboard::keys() const
    {
        std::vector<std::string> all;
        all.reserve(states_.size());
        for (const auto &entry : states_)
            all.push_back(entry.first);
        return all;
    }

    std::vector<OracleResolutionState> OracleResolutionDashboard::snapshots() const
    {
        std::vector<OracleResolutionState> all;
        all.reserve(states_.size());
        for (const auto &entry : states_)
            all.push_back(entry.second);
        return all;
    }

    nlohmann::json OracleResolutionDashboard::as_json() const
    {
        nlohmann::json out = nlohmann::json::array();
        for (const auto &entry : states_)
            out.push_back(oracle_resolution_state_to_json(entry.second));
        return out;
    }

    nlohmann::json OracleResolutionDashboard::as_json(const std::string &key) const
    {
        const auto *state = get(key);
        return state ? oracle_resolution_state_to_json(*state) : nlohmann::json::object();
    }

} // namespace polymarket
