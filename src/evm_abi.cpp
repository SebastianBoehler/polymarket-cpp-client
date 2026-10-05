#include "polymarket/evm_abi.hpp"
#include "evm_uint.hpp"
#include "polymarket/order_signer.hpp"
#include <algorithm>
#include <stdexcept>

namespace polymarket
{
    namespace
    {
        constexpr size_t kWordSize = 32;

        std::vector<uint8_t> parse_hex_bytes(const std::string &hex, const char *label)
        {
            try
            {
                return from_hex(hex);
            }
            catch (const std::invalid_argument &)
            {
                throw std::invalid_argument(std::string(label) + " is not valid hex");
            }
        }

        std::vector<uint8_t> word_from_bytes(const std::vector<uint8_t> &bytes, bool left_pad)
        {
            std::vector<uint8_t> word(kWordSize, 0);
            const auto start = left_pad ? word.begin() + (kWordSize - bytes.size()) : word.begin();
            std::copy(bytes.begin(), bytes.end(), start);
            return word;
        }

        void check_bits(unsigned bits)
        {
            if (bits == 0 || bits > 256 || bits % 8 != 0)
                throw std::invalid_argument("unsigned integer width must be 8..256 in steps of 8");
        }

        void check_fits(const std::vector<uint8_t> &word, unsigned bits)
        {
            const size_t high_bytes = kWordSize - bits / 8;
            for (size_t i = 0; i < high_bytes; ++i)
            {
                if (word[i] != 0)
                    throw std::invalid_argument("unsigned integer exceeds uint" + std::to_string(bits));
            }
        }

        void append_padded(std::vector<uint8_t> &out, const std::vector<uint8_t> &data)
        {
            out.insert(out.end(), data.begin(), data.end());
            const auto remainder = data.size() % kWordSize;
            if (remainder != 0)
                out.insert(out.end(), kWordSize - remainder, 0);
        }
    } // namespace

    EvmAbiValue EvmAbiValue::address(const std::string &hex)
    {
        const auto bytes = parse_hex_bytes(hex, "address");
        if (bytes.size() != 20)
            throw std::invalid_argument("address must be 20 bytes");
        EvmAbiValue value(Kind::Word);
        value.data_ = word_from_bytes(bytes, true);
        return value;
    }

    EvmAbiValue EvmAbiValue::unsigned_integer(const std::string &text, unsigned bits)
    {
        check_bits(bits);
        EvmAbiValue value(Kind::Word);
        value.data_ = detail::parse_uint256_word(text);
        check_fits(value.data_, bits);
        return value;
    }

    EvmAbiValue EvmAbiValue::unsigned_integer(uint64_t number, unsigned bits)
    {
        check_bits(bits);
        EvmAbiValue value(Kind::Word);
        value.data_ = detail::uint256_word(number);
        check_fits(value.data_, bits);
        return value;
    }

    EvmAbiValue EvmAbiValue::boolean(bool flag)
    {
        return unsigned_integer(flag ? 1u : 0u, 8);
    }

    EvmAbiValue EvmAbiValue::fixed_bytes(const std::string &hex, size_t size)
    {
        if (size == 0 || size > kWordSize)
            throw std::invalid_argument("fixed bytes size must be 1..32");
        const auto bytes = parse_hex_bytes(hex, "fixed bytes");
        if (bytes.size() != size)
            throw std::invalid_argument("bytes" + std::to_string(size) + " value must be " +
                                        std::to_string(size) + " bytes");
        EvmAbiValue value(Kind::Word);
        value.data_ = word_from_bytes(bytes, false);
        return value;
    }

    EvmAbiValue EvmAbiValue::bytes(const std::vector<uint8_t> &data)
    {
        EvmAbiValue value(Kind::Bytes);
        value.data_ = data;
        return value;
    }

    EvmAbiValue EvmAbiValue::bytes(const std::string &hex)
    {
        return bytes(parse_hex_bytes(hex, "bytes"));
    }

    EvmAbiValue EvmAbiValue::array(std::vector<EvmAbiValue> elements)
    {
        EvmAbiValue value(Kind::Array);
        value.children_ = std::move(elements);
        return value;
    }

    EvmAbiValue EvmAbiValue::tuple(std::vector<EvmAbiValue> members)
    {
        EvmAbiValue value(Kind::Tuple);
        value.children_ = std::move(members);
        return value;
    }

    bool EvmAbiValue::is_dynamic() const
    {
        switch (kind_)
        {
        case Kind::Word:
            return false;
        case Kind::Bytes:
        case Kind::Array:
            return true;
        case Kind::Tuple:
            for (const auto &member : children_)
            {
                if (member.is_dynamic())
                    return true;
            }
            return false;
        }
        return false;
    }

    void EvmAbiValue::append_encoding(std::vector<uint8_t> &out) const
    {
        switch (kind_)
        {
        case Kind::Word:
            out.insert(out.end(), data_.begin(), data_.end());
            return;
        case Kind::Bytes:
        {
            const auto length = detail::uint256_word(data_.size());
            out.insert(out.end(), length.begin(), length.end());
            append_padded(out, data_);
            return;
        }
        case Kind::Array:
        {
            const auto length = detail::uint256_word(children_.size());
            out.insert(out.end(), length.begin(), length.end());
            append_sequence(out, children_);
            return;
        }
        case Kind::Tuple:
            append_sequence(out, children_);
            return;
        }
    }

    // Head/tail layout: static members inline, dynamic members as a byte offset
    // (relative to the start of this sequence) into the tail.
    void EvmAbiValue::append_sequence(std::vector<uint8_t> &out,
                                      const std::vector<EvmAbiValue> &values)
    {
        std::vector<std::vector<uint8_t>> encoded;
        encoded.reserve(values.size());
        size_t head_size = 0;
        for (const auto &value : values)
        {
            std::vector<uint8_t> bytes;
            value.append_encoding(bytes);
            head_size += value.is_dynamic() ? kWordSize : bytes.size();
            encoded.push_back(std::move(bytes));
        }

        size_t tail_offset = head_size;
        for (size_t i = 0; i < values.size(); ++i)
        {
            if (values[i].is_dynamic())
            {
                const auto offset = detail::uint256_word(tail_offset);
                out.insert(out.end(), offset.begin(), offset.end());
                tail_offset += encoded[i].size();
            }
            else
            {
                out.insert(out.end(), encoded[i].begin(), encoded[i].end());
            }
        }
        for (size_t i = 0; i < values.size(); ++i)
        {
            if (values[i].is_dynamic())
                out.insert(out.end(), encoded[i].begin(), encoded[i].end());
        }
    }

    std::string evm_function_selector(const std::string &signature)
    {
        const auto hash = keccak256(signature);
        return to_hex(std::vector<uint8_t>(hash.begin(), hash.begin() + 4));
    }

    std::vector<uint8_t> evm_abi_encode(const std::vector<EvmAbiValue> &values)
    {
        std::vector<uint8_t> out;
        EvmAbiValue::append_sequence(out, values);
        return out;
    }

    std::string evm_abi_encode_call(const std::string &signature,
                                    const std::vector<EvmAbiValue> &arguments)
    {
        return evm_function_selector(signature) + to_hex(evm_abi_encode(arguments)).substr(2);
    }

} // namespace polymarket
