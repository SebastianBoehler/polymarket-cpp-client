#include "polymarket_events.hpp"
#include <array>
#include <unordered_map>

namespace polymarket
{
    namespace
    {
        struct EventSpec
        {
            PolymarketEventKind kind;
            const char *name;
            const char *signature;
        };

        constexpr std::array<EventSpec, 8> uma_specs{{
            {PolymarketEventKind::QuestionInitialized, "QuestionInitialized",
             "QuestionInitialized(bytes32,uint256,address,bytes,address,uint256,uint256)"},
            {PolymarketEventKind::QuestionPaused, "QuestionPaused", "QuestionPaused(bytes32)"},
            {PolymarketEventKind::QuestionUnpaused, "QuestionUnpaused", "QuestionUnpaused(bytes32)"},
            {PolymarketEventKind::QuestionFlagged, "QuestionFlagged", "QuestionFlagged(bytes32)"},
            {PolymarketEventKind::QuestionUnflagged, "QuestionUnflagged", "QuestionUnflagged(bytes32)"},
            {PolymarketEventKind::QuestionReset, "QuestionReset", "QuestionReset(bytes32)"},
            {PolymarketEventKind::QuestionResolved, "QuestionResolved",
             "QuestionResolved(bytes32,int256,uint256[])"},
            {PolymarketEventKind::QuestionManuallyResolved, "QuestionManuallyResolved",
             "QuestionManuallyResolved(bytes32,uint256[])"},
        }};

        constexpr std::array<EventSpec, 2> ctf_specs{{
            {PolymarketEventKind::ConditionResolution, "ConditionResolution",
             "ConditionResolution(bytes32,address,bytes32,uint256,uint256[])"},
            {PolymarketEventKind::PayoutRedemption, "PayoutRedemption",
             "PayoutRedemption(address,address,bytes32,bytes32,uint256[],uint256)"},
        }};

        std::vector<std::string> build_topics(const auto &specs)
        {
            std::vector<std::string> topics;
            topics.reserve(specs.size());
            for (const auto &spec : specs)
                topics.push_back(evm_event_topic(spec.signature));
            return topics;
        }

        const std::vector<std::string> &uma_topics_storage()
        {
            static const auto topics = build_topics(uma_specs);
            return topics;
        }

        const std::vector<std::string> &ctf_topics_storage()
        {
            static const auto topics = build_topics(ctf_specs);
            return topics;
        }

        const std::unordered_map<std::string, const EventSpec *> &specs_by_topic()
        {
            static const auto lookup = []()
            {
                std::unordered_map<std::string, const EventSpec *> result;
                result.reserve(uma_specs.size() + ctf_specs.size());
                const auto &uma_topics = uma_topics_storage();
                for (size_t index = 0; index < uma_specs.size(); ++index)
                    result.emplace(uma_topics[index], &uma_specs[index]);
                const auto &ctf_topics = ctf_topics_storage();
                for (size_t index = 0; index < ctf_specs.size(); ++index)
                    result.emplace(ctf_topics[index], &ctf_specs[index]);
                return result;
            }();
            return lookup;
        }

        const EventSpec *find_spec(const std::string &topic)
        {
            const auto &lookup = specs_by_topic();
            const auto found = lookup.find(topic);
            return found == lookup.end() ? nullptr : found->second;
        }

        void decode_question_id_event(PolymarketDecodedEvent &event)
        {
            if (event.raw_log.topics.size() > 1)
                event.question_id = event.raw_log.topics[1];
        }
    } // namespace

    std::string uma_event_topic(UmaEventKind kind)
    {
        return uma_topics_storage().at(static_cast<size_t>(kind));
    }

    std::string conditional_tokens_event_topic(ConditionalTokensEventKind kind)
    {
        return ctf_topics_storage().at(static_cast<size_t>(kind));
    }

    std::vector<std::string> uma_event_topics()
    {
        return uma_topics_storage();
    }

    std::vector<std::string> conditional_tokens_event_topics()
    {
        return ctf_topics_storage();
    }

    EvmLogFilter uma_adapter_log_filter(const std::string &adapter_address)
    {
        EvmLogFilter filter;
        filter.addresses = {evm_normalize_hex(adapter_address)};
        filter.topics = {uma_topics_storage()};
        return filter;
    }

    EvmLogFilter conditional_tokens_log_filter(const std::string &ctf_address)
    {
        EvmLogFilter filter;
        filter.addresses = {evm_normalize_hex(ctf_address)};
        filter.topics = {ctf_topics_storage()};
        return filter;
    }

    PolymarketDecodedEvent decode_polymarket_event(const EvmLog &log)
    {
        PolymarketDecodedEvent event;
        event.raw_log = log;
        if (log.topics.empty())
            return event;

        const auto *spec = find_spec(log.topics[0]);
        if (!spec)
            return event;

        event.kind = spec->kind;
        event.name = spec->name;

        switch (event.kind)
        {
        case PolymarketEventKind::QuestionInitialized:
        {
            decode_question_id_event(event);
            if (log.topics.size() > 2)
                event.request_timestamp = evm_uint_word_to_decimal(log.topics[2]);
            if (log.topics.size() > 3)
                event.creator = evm_word_to_address(log.topics[3]);
            auto words = evm_data_words(log.data);
            if (words.size() >= 4)
            {
                event.reward_token = evm_word_to_address(words[1]);
                event.reward = evm_uint_word_to_decimal(words[2]);
                event.proposal_bond = evm_uint_word_to_decimal(words[3]);
            }
            break;
        }
        case PolymarketEventKind::QuestionResolved:
            decode_question_id_event(event);
            if (log.topics.size() > 2)
                event.settled_price = evm_int_word_to_decimal(log.topics[2]);
            event.payouts = evm_decode_uint_array(log.data, 0, 1);
            break;
        case PolymarketEventKind::QuestionManuallyResolved:
            decode_question_id_event(event);
            event.payouts = evm_decode_uint_array(log.data, 0, 1);
            break;
        case PolymarketEventKind::ConditionResolution:
        {
            if (log.topics.size() > 1)
                event.condition_id = log.topics[1];
            if (log.topics.size() > 2)
                event.oracle = evm_word_to_address(log.topics[2]);
            if (log.topics.size() > 3)
                event.question_id = log.topics[3];
            auto words = evm_data_words(log.data);
            if (!words.empty())
                event.outcome_slot_count = evm_uint_word_to_decimal(words[0]);
            event.payouts = evm_decode_uint_array(log.data, 1, 2);
            break;
        }
        case PolymarketEventKind::PayoutRedemption:
        {
            if (log.topics.size() > 1)
                event.redeemer = evm_word_to_address(log.topics[1]);
            if (log.topics.size() > 2)
                event.collateral_token = evm_word_to_address(log.topics[2]);
            if (log.topics.size() > 3)
                event.parent_collection_id = log.topics[3];
            auto words = evm_data_words(log.data);
            if (words.size() >= 3)
            {
                event.condition_id = words[0];
                event.payout = evm_uint_word_to_decimal(words[2]);
            }
            event.index_sets = evm_decode_uint_array(log.data, 1, 3);
            break;
        }
        default:
            decode_question_id_event(event);
            break;
        }
        return event;
    }

} // namespace polymarket
