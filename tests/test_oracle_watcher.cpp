#include "oracle_watcher.hpp"
#include "polymarket_events.hpp"
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <vector>

using json = nlohmann::json;

namespace
{
void require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

std::string word(const std::string &hex)
{
    std::string clean = hex;
    if (clean.rfind("0x", 0) == 0)
    {
        clean = clean.substr(2);
    }
    return "0x" + std::string(64 - clean.size(), '0') + clean;
}

std::string repeated_word(char c)
{
    return "0x" + std::string(64, c);
}

std::string uint_array_data(const std::vector<std::string> &values)
{
    std::string out = word("20").substr(2) + word(std::to_string(values.size())).substr(2);
    for (const auto &value : values)
    {
        out += word(value).substr(2);
    }
    return "0x" + out;
}
}

int main()
{
    using namespace polymarket;

    OracleResolutionDashboard dashboard;
    const auto question_id = repeated_word('1');

    json init_raw = {
        {"address", "0x6A9D222616C90FcA5754cd1333cfd9b7fb6a4F74"},
        {"blockNumber", "0x10"},
        {"transactionHash", "0xinit"},
        {"blockHash", "0x01"},
        {"transactionIndex", "0x1"},
        {"logIndex", "0x1"},
        {"topics", json::array({uma_event_topic(UmaEventKind::QuestionInitialized),
                                 question_id,
                                 word("64"),
                                 word(std::string(40, 'a'))})},
        {"data", std::string("0x") +
                 word("0").substr(2) +
                 word("10").substr(2) +
                 word("11").substr(2) +
                 word("22").substr(2)}};

    auto init_decoded = decode_polymarket_event(evm_log_from_json(init_raw));
    require(init_decoded.kind == PolymarketEventKind::QuestionInitialized, "decode should parse question initialized");
    dashboard.ingest_event(init_decoded, false, 100);

    auto *state = dashboard.get(question_id);
    require(state != nullptr, "state should exist after init event");
    require(state->phase == OracleResolutionPhase::Pending, "initialized should map to pending phase");
    require(state->events_seen == 1, "one event should be counted after init");
    require(state->dispute_count == 0, "disputes should not start at init");
    require(state->question_id == question_id, "question id should be preserved");
    require(state->last_block == 16, "block should be parsed to integer");

    // Flagging is an adapter administration signal, not an OO dispute.
    json flagged_raw = {
        {"address", "0x6A9D222616C90FcA5754cd1333cfd9b7fb6a4F74"},
        {"blockNumber", "0x11"},
        {"transactionHash", "0xflag"},
        {"blockHash", "0x01"},
        {"transactionIndex", "0x2"},
        {"logIndex", "0x1"},
        {"topics", json::array({uma_event_topic(UmaEventKind::QuestionFlagged),
                                 question_id})},
        {"data", "0x"}};

    auto flagged_decoded = decode_polymarket_event(evm_log_from_json(flagged_raw));
    require(flagged_decoded.kind == PolymarketEventKind::QuestionFlagged, "decode should parse flagged");
    dashboard.ingest_event(flagged_decoded, true, 200);

    state = dashboard.get(question_id);
    require(state->phase == OracleResolutionPhase::Pending, "flag must not claim a dispute phase");
    require(state->dispute_count == 0, "flag must not increment dispute count");
    require(state->events_seen == 2, "two events should be counted before resolution");

    json resolved_raw = {
        {"address", "0x6A9D222616C90FcA5754cd1333cfd9b7fb6a4F74"},
        {"blockNumber", "0x12"},
        {"transactionHash", "0xresolved"},
        {"blockHash", "0x01"},
        {"transactionIndex", "0x3"},
        {"logIndex", "0x1"},
        {"topics", json::array({uma_event_topic(UmaEventKind::QuestionResolved),
                                 question_id,
                                 word("1")})},
        {"data", uint_array_data({"1", "0", "0", "1"})}};

    auto resolved_decoded = decode_polymarket_event(evm_log_from_json(resolved_raw));
    require(resolved_decoded.kind == PolymarketEventKind::QuestionResolved, "decode should parse resolved");
    require(resolved_decoded.settled_price == "1", "resolved event should decode settled price");
    require(resolved_decoded.payouts.size() == 4, "resolved data should decode full payout array");
    dashboard.ingest_event(resolved_decoded, true, 300);

    state = dashboard.get(question_id);
    require(state != nullptr, "state should still exist after resolved event");
    require(state->phase == OracleResolutionPhase::Resolved, "resolved should map to resolved phase");
    require(state->events_seen == 3, "three events should be counted total");
    require(state->dispute_count == 0, "resolution must not invent a dispute count");
    require(state->settled_price == "1", "state should preserve settled price");
    require(state->payouts.size() == 4, "state should persist decoded payouts");
    require(state->timeline.size() == 3, "timeline should include all accepted events");
    require(state->timeline.back().event_name == "QuestionResolved", "latest timeline event should be resolved");

    // A removed canonical log must roll the projection back to the state built
    // from the surviving events.
    resolved_raw["removed"] = true;
    auto removed_decoded = decode_polymarket_event(evm_log_from_json(resolved_raw));
    removed_decoded.raw_log.removed = true;
    dashboard.ingest_event(removed_decoded, false, 999);
    state = dashboard.get(question_id);
    require(state->phase == OracleResolutionPhase::Pending,
            "removing the resolution must restore the prior phase");
    require(state->events_seen == 2, "removed canonical event must leave the timeline");
    require(state->timeline.back().event_name == "QuestionFlagged",
            "timeline should end at the surviving event");
    require(state->settled_price.empty(), "removed resolution price must be cleared");
    require(state->payouts.empty(), "removed resolution payouts must be cleared");

    auto snapshot = oracle_resolution_state_to_json(*state);
    require(snapshot["phase"] == "PENDING", "json should expose rebuilt phase");
    require(snapshot["eventsSeen"] == 2, "json should expose rebuilt event count");

    // Inclusive cursor replay may deliver the last committed log again. The
    // projection must remain idempotent for the same canonical log identity.
    dashboard.ingest_event(flagged_decoded, false, 1000);
    state = dashboard.get(question_id);
    require(state->events_seen == 2, "replayed canonical log must not be counted twice");
    require(state->dispute_count == 0, "replayed flag must not create a dispute");

    OracleResolutionDashboard manual_dashboard;
    manual_dashboard.ingest_event(init_decoded, false, 100);
    auto reset_decoded = init_decoded;
    reset_decoded.kind = PolymarketEventKind::QuestionReset;
    reset_decoded.name = "QuestionReset";
    reset_decoded.raw_log.block_number = "0x11";
    reset_decoded.raw_log.transaction_hash = "0xreset";
    reset_decoded.raw_log.log_index = "0x2";
    manual_dashboard.ingest_event(reset_decoded, true, 200);
    const auto *manual_state = manual_dashboard.get(question_id);
    require(manual_state && manual_state->phase == OracleResolutionPhase::Pending,
            "reset should map to pending phase");
    require(manual_state->dispute_count == 0, "reset must not create a dispute");

    auto manual_decoded = resolved_decoded;
    manual_decoded.kind = PolymarketEventKind::QuestionManuallyResolved;
    manual_decoded.name = "QuestionManuallyResolved";
    manual_decoded.raw_log.block_number = "0x13";
    manual_decoded.raw_log.transaction_hash = "0xmanual";
    manual_dashboard.ingest_event(manual_decoded, true, 300);
    manual_state = manual_dashboard.get(question_id);
    require(manual_state && manual_state->phase == OracleResolutionPhase::ManuallyResolved,
            "manual resolution should map to manually resolved phase");
    require(manual_state->dispute_count == 0, "manual resolution must not create a dispute");

    OracleResolutionDashboard reordered_dashboard;
    reordered_dashboard.ingest_event(init_decoded, false, 100);
    reordered_dashboard.ingest_event(flagged_decoded, true, 200);
    reordered_dashboard.ingest_event(resolved_decoded, true, 300);
    auto removed_flag = flagged_decoded;
    removed_flag.raw_log.removed = true;
    reordered_dashboard.ingest_event(removed_flag, true, 400);
    auto replacement_flag = flagged_decoded;
    replacement_flag.raw_log.block_hash = "0x02";
    replacement_flag.raw_log.transaction_hash = "0xflag-replacement";
    reordered_dashboard.ingest_event(replacement_flag, true, 500);
    const auto *reordered = reordered_dashboard.get(question_id);
    require(reordered && reordered->phase == OracleResolutionPhase::Resolved,
            "earlier replacement must not regress state behind a later resolution");
    require(reordered->timeline.size() == 3 &&
                reordered->timeline[1].transaction_hash == "0xflag-replacement" &&
                reordered->timeline[2].transaction_hash == "0xresolved",
            "replacement history must be ordered canonically by log position");

    // Aggregate counters cover the full stream, while the materialized
    // timeline retains only the bounded rollback window.
    OracleResolutionDashboard bounded_dashboard;
    constexpr std::size_t retained_events =
        OracleResolutionDashboard::retained_history_events_per_key;
    for (std::size_t index = 0; index < retained_events + 8; ++index)
    {
        auto event = flagged_decoded;
        event.raw_log.block_number = std::to_string(index + 1);
        event.raw_log.block_hash = "0xbounded-block-" + std::to_string(index);
        event.raw_log.transaction_hash = "0xbounded-tx-" + std::to_string(index);
        bounded_dashboard.ingest_event(event, true, index);
    }
    const auto *bounded = bounded_dashboard.get(question_id);
    require(bounded && bounded->events_seen == retained_events + 8,
            "compaction must preserve aggregate event counts");
    require(bounded->timeline.size() == retained_events,
            "dashboard timeline must stay within its retained-history contract");
    require(bounded->timeline.front().block_number == "9",
            "dashboard timeline must retain the newest canonical events");

    // Verify key lookup and state list APIs around resolved markets.
    const auto keys = dashboard.keys();
    bool found = false;
    for (const auto &k : keys)
    {
        if (k == question_id)
            found = true;
    }
    require(found, "keys() should include the resolved market key");

    const auto all_states = dashboard.snapshots();
    require(all_states.size() == 1, "snapshots should return all market states");

    // CTF condition resolution with missing indexed question id should fall back to condition id key.
    OracleResolutionDashboard ctf_dashboard;
    const auto condition_id = repeated_word('f');
    json condition_raw = {
        {"address", "0x4D97DCd97eC945f40cF65F87097ACe5EA0476045"},
        {"blockNumber", "0x20"},
        {"transactionHash", "0xctf"},
        {"blockHash", "0x01"},
        {"transactionIndex", "0x1"},
        {"logIndex", "0x1"},
        {"topics", json::array({conditional_tokens_event_topic(ConditionalTokensEventKind::ConditionResolution),
                                 condition_id,
                                 word(std::string(40, 'a'))})},
        {"data", std::string("0x") +
                 word("2").substr(2) +
                 word("40").substr(2) +
                 word("2").substr(2) +
                 word("1").substr(2) +
                 word("0").substr(2)}};
    auto ctf_decoded = decode_polymarket_event(evm_log_from_json(condition_raw));
    require(ctf_decoded.kind == PolymarketEventKind::ConditionResolution, "decode should parse condition resolution");
    require(ctf_decoded.condition_id == condition_id, "ctf should parse condition id");
    require(ctf_decoded.question_id.empty(), "ctf test log intentionally omits question id");

    ctf_dashboard.ingest_event(ctf_decoded, true, 500);
    auto *ctf_state = ctf_dashboard.get(condition_id);
    require(ctf_state != nullptr, "ctf state should be keyed by condition id when question id is absent");
    require(ctf_state->phase == OracleResolutionPhase::Resolved, "ctf should map to resolved phase");
    require(ctf_state->payouts.size() == 2, "ctf payouts should be parsed");
    require(ctf_state->condition_id == condition_id, "condition id should be retained");

    auto ctf_json = ctf_dashboard.as_json();
    require(ctf_json.is_array(), "ctf dashboard json should be an array");

    std::cout << "test_oracle_watcher passed\n";
    return 0;
}
