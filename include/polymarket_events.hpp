#pragma once

#include "evm_utils.hpp"
#include <string>
#include <vector>

namespace polymarket
{

    enum class UmaEventKind
    {
        QuestionInitialized,
        QuestionPaused,
        QuestionUnpaused,
        QuestionFlagged,
        QuestionUnflagged,
        QuestionReset,
        QuestionResolved,
        QuestionManuallyResolved
    };

    enum class ConditionalTokensEventKind
    {
        ConditionResolution,
        PayoutRedemption
    };

    enum class PolymarketEventKind
    {
        Unknown,
        QuestionInitialized,
        QuestionPaused,
        QuestionUnpaused,
        QuestionFlagged,
        QuestionUnflagged,
        QuestionReset,
        QuestionResolved,
        QuestionManuallyResolved,
        ConditionResolution,
        PayoutRedemption
    };

    struct PolymarketDecodedEvent
    {
        PolymarketEventKind kind{PolymarketEventKind::Unknown};
        std::string name{"Unknown"};
        EvmLog raw_log;

        std::string question_id;
        std::string condition_id;
        std::string request_timestamp;
        std::string creator;
        std::string reward_token;
        std::string reward;
        std::string proposal_bond;
        std::string oracle;
        std::string settled_price;
        std::string outcome_slot_count;
        std::string redeemer;
        std::string collateral_token;
        std::string parent_collection_id;
        std::string payout;

        std::vector<std::string> payouts;
        std::vector<std::string> index_sets;
    };

    std::string uma_event_topic(UmaEventKind kind);
    std::string conditional_tokens_event_topic(ConditionalTokensEventKind kind);

    std::vector<std::string> uma_event_topics();
    std::vector<std::string> conditional_tokens_event_topics();

    EvmLogFilter uma_adapter_log_filter(const std::string &adapter_address);
    EvmLogFilter conditional_tokens_log_filter(const std::string &ctf_address);

    PolymarketDecodedEvent decode_polymarket_event(const EvmLog &log);

} // namespace polymarket
