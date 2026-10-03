#include "evm_uint.hpp"
#include "order_signer.hpp"
#include <stdexcept>

namespace polymarket::detail
{
    namespace
    {
        constexpr size_t kWordSize = 32;

        std::vector<uint8_t> decimal_to_word(const std::string &decimal)
        {
            std::vector<uint8_t> word(kWordSize, 0);
            for (const char character : decimal)
            {
                if (character < '0' || character > '9')
                    throw std::invalid_argument("unsigned integer must be base-10 digits or 0x-hex");
                unsigned carry = static_cast<unsigned>(character - '0');
                for (auto it = word.rbegin(); it != word.rend(); ++it)
                {
                    const unsigned next = static_cast<unsigned>(*it) * 10 + carry;
                    *it = static_cast<uint8_t>(next & 0xff);
                    carry = next >> 8;
                }
                if (carry != 0)
                    throw std::invalid_argument("unsigned integer exceeds uint256");
            }
            return word;
        }

        std::vector<uint8_t> hex_to_word(std::string digits)
        {
            const auto first = digits.find_first_not_of('0');
            digits = first == std::string::npos ? "" : digits.substr(first);
            if (digits.size() > 2 * kWordSize)
                throw std::invalid_argument("unsigned integer exceeds uint256");
            if (digits.size() % 2 != 0)
                digits.insert(digits.begin(), '0');
            std::vector<uint8_t> bytes;
            try
            {
                bytes = from_hex(digits);
            }
            catch (const std::invalid_argument &)
            {
                throw std::invalid_argument("unsigned integer is not valid hex");
            }
            std::vector<uint8_t> word(kWordSize - bytes.size(), 0);
            word.insert(word.end(), bytes.begin(), bytes.end());
            return word;
        }
    } // namespace

    std::vector<uint8_t> parse_uint256_word(const std::string &text)
    {
        const bool is_hex = text.rfind("0x", 0) == 0 || text.rfind("0X", 0) == 0;
        if (text.empty() || (is_hex && text.size() == 2))
            throw std::invalid_argument("unsigned integer is empty");
        return is_hex ? hex_to_word(text.substr(2)) : decimal_to_word(text);
    }

    std::vector<uint8_t> uint256_word(uint64_t value)
    {
        std::vector<uint8_t> word(kWordSize, 0);
        for (size_t i = 0; i < sizeof(value); ++i)
            word[kWordSize - 1 - i] = static_cast<uint8_t>((value >> (8 * i)) & 0xff);
        return word;
    }
}
