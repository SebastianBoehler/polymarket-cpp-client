#pragma once

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace polymarket
{

    struct EvmLog
    {
        std::string address;
        std::string block_hash;
        std::string block_number;
        std::string transaction_hash;
        std::string transaction_index;
        std::string log_index;
        std::string data;
        std::vector<std::string> topics;
        bool removed{false};
    };

    struct EvmLogFilter
    {
        std::vector<std::string> addresses;
        std::vector<std::vector<std::string>> topics;
        std::string from_block;
        std::string to_block;

        nlohmann::json to_json() const;
    };

    std::string evm_normalize_hex(std::string value);
    std::string evm_event_topic(const std::string &event_signature);
    EvmLog evm_log_from_json(const nlohmann::json &raw);

    std::vector<std::string> evm_data_words(const std::string &data);
    std::vector<std::string> evm_decode_uint_array(const std::string &data,
                                                   size_t offset_word_index);
    std::string evm_word_to_address(const std::string &word);
    std::string evm_uint_word_to_decimal(const std::string &word);
    std::string evm_int_word_to_decimal(const std::string &word);

} // namespace polymarket
