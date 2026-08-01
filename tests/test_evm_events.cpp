#include "evm_utils.hpp"
#include "polymarket_events.hpp"
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>

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
} // namespace

int main()
{
    using namespace polymarket;

    require(evm_event_topic("Transfer(address,address,uint256)") ==
                "0xddf252ad1be2c89b69c2b068fc378daa952ba7f163c4a11628f55a4df523b3ef",
            "event topic hash mismatch");

    EvmLogFilter filter;
    filter.addresses = {"0xabc0000000000000000000000000000000000000"};
    filter.topics = {{"0x01", "0x02"}, {}, {"0x03"}};
    auto filter_json = filter.to_json();
    require(filter_json["address"][0] == "0xabc0000000000000000000000000000000000000", "filter address mismatch");
    require(filter_json["topics"][0][1] == "0x02", "filter topic mismatch");
    require(filter_json["topics"][1].is_null(), "empty topic slot should become null");
    require(filter_json["topics"][2][0] == "0x03", "third topic mismatch");

    const auto uma_topics = uma_event_topics();
    const auto ctf_topics = conditional_tokens_event_topics();
    require(uma_topics.size() == 8 && ctf_topics.size() == 2,
            "public topic accessors preserve their by-value API and complete topic sets");

    const auto require_array_rejected = [](const std::string &data, const char *message,
                                           size_t offset_word = 0)
    {
        bool rejected = false;
        try
        {
            (void)evm_decode_uint_array(data, offset_word);
        }
        catch (const std::exception &)
        {
            rejected = true;
        }
        require(rejected, message);
    };
    const auto array_offset = word("20").substr(2);
    require_array_rejected("0x" + array_offset + word("ffffffffffffffff").substr(2),
                           "maximum uint64 array length must not overflow bounds check");
    require_array_rejected("0x" + std::string(64, 'f') + word("0").substr(2),
                           "oversized ABI offset must be rejected");
    require_array_rejected("0x" + array_offset + std::string(63, '0') + "g",
                           "non-hex ABI length word must be rejected");
    require_array_rejected("0x" + word("0").substr(2),
                           "single dynamic array offset must not point into its head");
    std::string condition_head_alias = "0x" + word("2").substr(2) +
                                       word("20").substr(2);
    for (size_t index = 0; index < 32; ++index)
        condition_head_alias += word("0").substr(2);
    require_array_rejected(condition_head_alias,
                           "condition array offset must follow its two-word head", 1);

    bool padded_address_rejected = false;
    try
    {
        (void)evm_word_to_address("0x000000000000000000000001" +
                                  std::string(40, 'a'));
    }
    catch (const std::exception &)
    {
        padded_address_rejected = true;
    }
    require(padded_address_rejected,
            "ABI address word must have canonical zero high padding");

    const auto require_integer_word_rejected = [](const std::string &value)
    {
        bool unsigned_rejected = false;
        bool signed_rejected = false;
        try
        {
            (void)evm_uint_word_to_decimal(value);
        }
        catch (const std::exception &)
        {
            unsigned_rejected = true;
        }
        try
        {
            (void)evm_int_word_to_decimal(value);
        }
        catch (const std::exception &)
        {
            signed_rejected = true;
        }
        require(unsigned_rejected && signed_rejected,
                "integer decoders must reject noncanonical ABI words");
    };
    require_integer_word_rejected("0x");
    require_integer_word_rejected("0x1");
    require_integer_word_rejected("0x" + std::string(65, '0'));
    require_integer_word_rejected("0x" + std::string(63, '0') + "g");

    json raw_log = {
        {"address", "0x6A9D222616C90FcA5754cd1333cFD9b7fb6a4F74"},
        {"blockNumber", "0x10"},
        {"transactionHash", "0xtx"},
        {"logIndex", "0x2"},
        {"data", uint_array_data({"0", "1"})},
        {"topics", json::array({uma_event_topic(UmaEventKind::QuestionResolved),
                                 repeated_word('1'),
                                 word("1")})}};

    auto decoded = decode_polymarket_event(evm_log_from_json(raw_log));
    require(decoded.kind == PolymarketEventKind::QuestionResolved, "wrong UMA event kind");
    require(decoded.name == "QuestionResolved", "wrong UMA event name");
    require(decoded.question_id == repeated_word('1'), "wrong UMA question id");
    require(decoded.settled_price == "1", "wrong UMA settled price");
    require(decoded.payouts.size() == 2, "wrong UMA payout count");
    require(decoded.payouts[0] == "0", "wrong UMA payout 0");
    require(decoded.payouts[1] == "1", "wrong UMA payout 1");

    json condition_log = {
        {"address", "0x4D97DCd97eC945f40cF65F87097ACe5EA0476045"},
        {"data", word("2").substr(2) + word("40").substr(2) +
                     word("2").substr(2) + word("1").substr(2) + word("0").substr(2)},
        {"topics", json::array({conditional_tokens_event_topic(ConditionalTokensEventKind::ConditionResolution),
                                 repeated_word('2'),
                                 "0x0000000000000000000000006a9d222616c90fca5754cd1333cfd9b7fb6a4f74",
                                 repeated_word('3')})}};
    condition_log["data"] = "0x" + condition_log["data"].get<std::string>();

    auto condition = decode_polymarket_event(evm_log_from_json(condition_log));
    require(condition.kind == PolymarketEventKind::ConditionResolution, "wrong CTF event kind");
    require(condition.condition_id == repeated_word('2'), "wrong condition id");
    require(condition.oracle == "0x6a9d222616c90fca5754cd1333cfd9b7fb6a4f74", "wrong oracle");
    require(condition.question_id == repeated_word('3'), "wrong CTF question id");
    require(condition.outcome_slot_count == "2", "wrong outcome slot count");
    require(condition.payouts.size() == 2, "wrong CTF payout count");
    require(condition.payouts[0] == "1", "wrong CTF payout 0");
    require(condition.payouts[1] == "0", "wrong CTF payout 1");

    std::cout << "test_evm_events passed\n";
    return 0;
}
