#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace polymarket
{
    // One Solidity ABI value. Build values with the static factories and pass
    // them to evm_abi_encode or evm_abi_encode_call. Factories validate their
    // input and throw std::invalid_argument on malformed or out-of-range data.
    class EvmAbiValue
    {
    public:
        // address: 20 bytes of hex, with or without 0x. Checksum case is not enforced.
        static EvmAbiValue address(const std::string &hex);
        // uintN from a base-10 string, or from a 0x-prefixed hex string. bits is 8..256 in steps of 8.
        static EvmAbiValue unsigned_integer(const std::string &value, unsigned bits = 256);
        static EvmAbiValue unsigned_integer(uint64_t value, unsigned bits = 256);
        static EvmAbiValue uint256(const std::string &value) { return unsigned_integer(value, 256); }
        static EvmAbiValue uint256(uint64_t value) { return unsigned_integer(value, 256); }
        static EvmAbiValue boolean(bool value);
        // bytesN (1..32): exactly N bytes of hex, left-aligned in the word.
        static EvmAbiValue fixed_bytes(const std::string &hex, size_t size);
        static EvmAbiValue bytes32(const std::string &hex) { return fixed_bytes(hex, 32); }
        // Dynamic bytes.
        static EvmAbiValue bytes(const std::vector<uint8_t> &data);
        static EvmAbiValue bytes(const std::string &hex);
        // Dynamic T[]. Elements are not type-checked against each other; callers
        // must pass values of one Solidity type.
        static EvmAbiValue array(std::vector<EvmAbiValue> elements);
        static EvmAbiValue tuple(std::vector<EvmAbiValue> members);

        bool is_dynamic() const;

    private:
        enum class Kind
        {
            Word,
            Bytes,
            Array,
            Tuple
        };

        explicit EvmAbiValue(Kind kind) : kind_(kind) {}

        Kind kind_;
        std::vector<uint8_t> data_;          // Word: 32 bytes; Bytes: raw payload
        std::vector<EvmAbiValue> children_;  // Array elements or tuple members

        void append_encoding(std::vector<uint8_t> &out) const;
        static void append_sequence(std::vector<uint8_t> &out,
                                    const std::vector<EvmAbiValue> &values);

        friend std::vector<uint8_t> evm_abi_encode(const std::vector<EvmAbiValue> &values);
    };

    // 4-byte selector of a canonical signature such as "transfer(address,uint256)", as 0x-hex.
    std::string evm_function_selector(const std::string &signature);

    // Standard ABI encoding of a parameter list (as if it were one tuple).
    std::vector<uint8_t> evm_abi_encode(const std::vector<EvmAbiValue> &values);

    // Selector followed by encoded arguments, as lowercase 0x-hex calldata.
    std::string evm_abi_encode_call(const std::string &signature,
                                    const std::vector<EvmAbiValue> &arguments);

} // namespace polymarket
