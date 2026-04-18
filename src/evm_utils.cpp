#include "evm_utils.hpp"
#include "order_signer.hpp"
#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace polymarket
{
    namespace
    {
        std::string strip_0x(std::string value)
        {
            if (value.rfind("0x", 0) == 0 || value.rfind("0X", 0) == 0)
            {
                value = value.substr(2);
            }
            return value;
        }

        int hex_value(char c)
        {
            if (c >= '0' && c <= '9')
                return c - '0';
            if (c >= 'a' && c <= 'f')
                return 10 + c - 'a';
            if (c >= 'A' && c <= 'F')
                return 10 + c - 'A';
            throw std::invalid_argument("invalid hex character");
        }

        std::string decimal_mul_add(std::string decimal, int mul, int add)
        {
            int carry = add;
            for (auto it = decimal.rbegin(); it != decimal.rend(); ++it)
            {
                int next = (*it - '0') * mul + carry;
                *it = static_cast<char>('0' + (next % 10));
                carry = next / 10;
            }
            while (carry > 0)
            {
                decimal.insert(decimal.begin(), static_cast<char>('0' + (carry % 10)));
                carry /= 10;
            }
            auto pos = decimal.find_first_not_of('0');
            return pos == std::string::npos ? "0" : decimal.substr(pos);
        }

        std::string positive_hex_to_decimal(const std::string &hex)
        {
            std::string result = "0";
            for (char c : strip_0x(hex))
            {
                result = decimal_mul_add(result, 16, hex_value(c));
            }
            return result;
        }

        std::string twos_complement_abs(std::string hex)
        {
            hex = strip_0x(hex);
            if (hex.size() < 64)
                hex.insert(hex.begin(), 64 - hex.size(), '0');
            for (char &c : hex)
            {
                int inverted = 15 - hex_value(c);
                c = static_cast<char>(inverted < 10 ? '0' + inverted : 'a' + inverted - 10);
            }
            int carry = 1;
            for (auto it = hex.rbegin(); it != hex.rend() && carry; ++it)
            {
                int next = hex_value(*it) + carry;
                carry = next >> 4;
                next &= 0xf;
                *it = static_cast<char>(next < 10 ? '0' + next : 'a' + next - 10);
            }
            return hex;
        }
    } // namespace

    nlohmann::json EvmLogFilter::to_json() const
    {
        nlohmann::json out = nlohmann::json::object();
        if (!addresses.empty())
        {
            out["address"] = addresses;
        }
        if (!topics.empty())
        {
            out["topics"] = nlohmann::json::array();
            for (const auto &slot : topics)
            {
                out["topics"].push_back(slot.empty() ? nlohmann::json(nullptr)
                                                     : nlohmann::json(slot));
            }
        }
        if (!from_block.empty())
            out["fromBlock"] = from_block;
        if (!to_block.empty())
            out["toBlock"] = to_block;
        return out;
    }

    std::string evm_normalize_hex(std::string value)
    {
        value = strip_0x(value);
        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return "0x" + value;
    }

    std::string evm_event_topic(const std::string &event_signature)
    {
        return evm_normalize_hex(to_hex(keccak256(event_signature)));
    }

    EvmLog evm_log_from_json(const nlohmann::json &raw)
    {
        EvmLog log;
        log.address = evm_normalize_hex(raw.value("address", ""));
        log.block_hash = evm_normalize_hex(raw.value("blockHash", ""));
        log.block_number = raw.value("blockNumber", "");
        log.transaction_hash = raw.value("transactionHash", "");
        log.transaction_index = raw.value("transactionIndex", "");
        log.log_index = raw.value("logIndex", "");
        log.data = evm_normalize_hex(raw.value("data", "0x"));
        log.removed = raw.value("removed", false);
        if (raw.contains("topics") && raw["topics"].is_array())
        {
            for (const auto &topic : raw["topics"])
            {
                log.topics.push_back(evm_normalize_hex(topic.get<std::string>()));
            }
        }
        return log;
    }

    std::vector<std::string> evm_data_words(const std::string &data)
    {
        std::string hex = strip_0x(data);
        if (hex.size() % 64 != 0)
            throw std::invalid_argument("ABI data is not word-aligned");

        std::vector<std::string> words;
        for (size_t i = 0; i < hex.size(); i += 64)
        {
            words.push_back("0x" + hex.substr(i, 64));
        }
        return words;
    }

    std::vector<std::string> evm_decode_uint_array(const std::string &data,
                                                   size_t offset_word_index)
    {
        auto words = evm_data_words(data);
        if (offset_word_index >= words.size())
            throw std::invalid_argument("array offset word is missing");

        auto offset_bytes = std::stoull(strip_0x(words[offset_word_index]), nullptr, 16);
        if (offset_bytes % 32 != 0)
            throw std::invalid_argument("array offset is not word-aligned");

        size_t length_index = static_cast<size_t>(offset_bytes / 32);
        if (length_index >= words.size())
            throw std::invalid_argument("array length word is missing");

        auto length = std::stoull(strip_0x(words[length_index]), nullptr, 16);
        if (length_index + 1 + length > words.size())
            throw std::invalid_argument("array data is truncated");

        std::vector<std::string> values;
        for (size_t i = 0; i < length; ++i)
        {
            values.push_back(evm_uint_word_to_decimal(words[length_index + 1 + i]));
        }
        return values;
    }

    std::string evm_word_to_address(const std::string &word)
    {
        auto hex = strip_0x(word);
        if (hex.size() < 40)
            throw std::invalid_argument("word is too short for address");
        return evm_normalize_hex(hex.substr(hex.size() - 40));
    }

    std::string evm_uint_word_to_decimal(const std::string &word)
    {
        return positive_hex_to_decimal(word);
    }

    std::string evm_int_word_to_decimal(const std::string &word)
    {
        auto hex = strip_0x(word);
        if (hex.empty())
            return "0";
        if (hex.size() < 64)
            hex.insert(hex.begin(), 64 - hex.size(), '0');
        bool negative = hex_value(hex.front()) >= 8;
        if (!negative)
            return positive_hex_to_decimal(hex);
        return "-" + positive_hex_to_decimal(twos_complement_abs(hex));
    }

} // namespace polymarket
