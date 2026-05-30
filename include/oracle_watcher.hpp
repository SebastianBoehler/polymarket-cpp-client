#pragma once

#include "polymarket_events.hpp"
#include <nlohmann/json.hpp>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace polymarket
{
    // In-memory projector that turns Polymarket oracle-related chain events into
    // normalized resolution state. This is a library abstraction for callers that
    // want structured oracle status, not a runtime watcher process.

    enum class OracleResolutionPhase
    {
        Unknown,
        Pending,
        Proposed,
        Disputed,
        Resolved,
        ManuallyResolved
    };

    struct OracleResolutionEvent
    {
        std::string phase_name;
        std::string event_name;
        bool live{false};
        uint64_t received_at_ms{0};
        std::string block_number;
        std::string transaction_hash;
        std::string question_id;
        std::string condition_id;
        std::string settled_price;
        std::vector<std::string> payouts;
        std::string payout;
        bool removed{false};
    };

    struct OracleResolutionState
    {
        std::string key;
        std::string question_id;
        std::string condition_id;
        OracleResolutionPhase phase{OracleResolutionPhase::Unknown};
        uint32_t events_seen{0};
        uint32_t dispute_count{0};
        uint64_t last_seen_ms{0};
        uint64_t last_block{0};
        std::string last_tx_hash;
        std::string last_event_name;
        std::string settled_price;
        std::vector<std::string> payouts;
        std::vector<OracleResolutionEvent> timeline;
    };

    std::string oracle_resolution_phase_to_string(OracleResolutionPhase phase);

    nlohmann::json oracle_resolution_event_to_json(const OracleResolutionEvent &event);
    nlohmann::json oracle_resolution_state_to_json(const OracleResolutionState &state);

    class OracleResolutionDashboard
    {
    public:
        void ingest_event(const PolymarketDecodedEvent &event, bool live, uint64_t received_at_ms);

        const OracleResolutionState *get(const std::string &key) const;
        std::vector<std::string> keys() const;
        std::vector<OracleResolutionState> snapshots() const;

        nlohmann::json as_json() const;
        nlohmann::json as_json(const std::string &key) const;

        static std::string state_key(const PolymarketDecodedEvent &event);

    private:
        std::unordered_map<std::string, OracleResolutionState> states_;

        OracleResolutionPhase next_phase(PolymarketEventKind kind) const;
        void append_event(OracleResolutionState &state,
                          const PolymarketDecodedEvent &event, bool live, uint64_t received_at_ms);
        void touch_ids(OracleResolutionState &state, const PolymarketDecodedEvent &event);
        std::string parse_block_number(const std::string &raw) const;
    };

} // namespace polymarket
