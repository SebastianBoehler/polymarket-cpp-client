#include "check_support.hpp"
#include "polymarket/evm_abi.hpp"
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

// Encodings of the shapes later phases need (tuple arrays, bytes, bytes31,
// nested dynamics), checked against eth_abi 5.x, the encoder the py-sdk uses.

namespace
{
    using check_support::expect_equal;
    using check_support::expect_throws;
    using polymarket::EvmAbiValue;

    const std::string kCtf = "0x4D97DCd97eC945f40cF65F87097ACe5EA0476045";
    const std::string kNegRiskAdapter = "0xd91E80cF2E7be2e162c6513ceD06f1dD0dA35296";

    std::vector<EvmAbiValue> binary_partition()
    {
        return {EvmAbiValue::uint256(1), EvmAbiValue::uint256(2)};
    }

    void eth_abi_vectors()
    {
        // ProxyWalletFactory batch: dynamic array of dynamic tuples.
        expect_equal("proxy tuple array",
                     polymarket::evm_abi_encode_call(
                         "proxy((uint8,address,uint256,bytes)[])",
                         {EvmAbiValue::array({
                             EvmAbiValue::tuple({EvmAbiValue::unsigned_integer(1, 8),
                                                 EvmAbiValue::address(kCtf), EvmAbiValue::uint256(0),
                                                 EvmAbiValue::bytes("0x72ce427511111111")}),
                             EvmAbiValue::tuple({EvmAbiValue::unsigned_integer(1, 8),
                                                 EvmAbiValue::address(kNegRiskAdapter),
                                                 EvmAbiValue::uint256(5), EvmAbiValue::bytes("0xabcdef")}),
                         })}),
                     "0x34ee9791"
                     "0000000000000000000000000000000000000000000000000000000000000020"
                     "0000000000000000000000000000000000000000000000000000000000000002"
                     "0000000000000000000000000000000000000000000000000000000000000040"
                     "0000000000000000000000000000000000000000000000000000000000000100"
                     "0000000000000000000000000000000000000000000000000000000000000001"
                     "0000000000000000000000004d97dcd97ec945f40cf65f87097ace5ea0476045"
                     "0000000000000000000000000000000000000000000000000000000000000000"
                     "0000000000000000000000000000000000000000000000000000000000000080"
                     "0000000000000000000000000000000000000000000000000000000000000008"
                     "72ce427511111111000000000000000000000000000000000000000000000000"
                     "0000000000000000000000000000000000000000000000000000000000000001"
                     "000000000000000000000000d91e80cf2e7be2e162c6513ced06f1dd0da35296"
                     "0000000000000000000000000000000000000000000000000000000000000005"
                     "0000000000000000000000000000000000000000000000000000000000000080"
                     "0000000000000000000000000000000000000000000000000000000000000003"
                     "abcdef0000000000000000000000000000000000000000000000000000000000");

        // Protocol V2 router: bytes31 is left-aligned.
        expect_equal("router split bytes31",
                     polymarket::evm_abi_encode_call(
                         "split(bytes31,uint256)",
                         {EvmAbiValue::fixed_bytes("0x" + std::string(62, '1'), 31),
                          EvmAbiValue::uint256(1000000)}),
                     "0x82d3b9f1"
                     "1111111111111111111111111111111111111111111111111111111111111100"
                     "00000000000000000000000000000000000000000000000000000000000f4240");

        std::vector<uint8_t> multisend_payload;
        for (uint8_t b = 1; b <= 33; ++b)
            multisend_payload.push_back(b);
        expect_equal("multiSend bytes over one word",
                     polymarket::evm_abi_encode_call("multiSend(bytes)",
                                                     {EvmAbiValue::bytes(multisend_payload)}),
                     "0x8d80ff0a"
                     "0000000000000000000000000000000000000000000000000000000000000020"
                     "0000000000000000000000000000000000000000000000000000000000000021"
                     "0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20"
                     "2100000000000000000000000000000000000000000000000000000000000000");

        expect_equal("empty array and bytes",
                     polymarket::evm_abi_encode_call(
                         "f(uint256[],bytes)", {EvmAbiValue::array({}), EvmAbiValue::bytes(std::vector<uint8_t>{})}),
                     "0x510dd1f5"
                     "0000000000000000000000000000000000000000000000000000000000000040"
                     "0000000000000000000000000000000000000000000000000000000000000060"
                     "0000000000000000000000000000000000000000000000000000000000000000"
                     "0000000000000000000000000000000000000000000000000000000000000000");

        expect_equal("max uint, bool, uint8, bytes4",
                     polymarket::evm_abi_encode_call(
                         "g(uint256,bool,uint8,bytes4)",
                         {EvmAbiValue::uint256("0x" + std::string(64, 'f')), EvmAbiValue::boolean(true),
                          EvmAbiValue::unsigned_integer("255", 8), EvmAbiValue::fixed_bytes("0xdeadbeef", 4)}),
                     "0xafe4c3b5"
                     "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
                     "0000000000000000000000000000000000000000000000000000000000000001"
                     "00000000000000000000000000000000000000000000000000000000000000ff"
                     "deadbeef00000000000000000000000000000000000000000000000000000000");

        // Dynamic tuple gets an offset; static tuple is inlined in the head.
        expect_equal("dynamic and static tuple params",
                     polymarket::evm_abi_encode_call(
                         "h((address,bytes),uint256,(uint256,address))",
                         {EvmAbiValue::tuple({EvmAbiValue::address(kCtf), EvmAbiValue::bytes("0x01")}),
                          EvmAbiValue::uint256(7),
                          EvmAbiValue::tuple({EvmAbiValue::uint256(9), EvmAbiValue::address(kNegRiskAdapter)})}),
                     "0xcb897565"
                     "0000000000000000000000000000000000000000000000000000000000000080"
                     "0000000000000000000000000000000000000000000000000000000000000007"
                     "0000000000000000000000000000000000000000000000000000000000000009"
                     "000000000000000000000000d91e80cf2e7be2e162c6513ced06f1dd0da35296"
                     "0000000000000000000000004d97dcd97ec945f40cf65f87097ace5ea0476045"
                     "0000000000000000000000000000000000000000000000000000000000000040"
                     "0000000000000000000000000000000000000000000000000000000000000001"
                     "0100000000000000000000000000000000000000000000000000000000000000");

        expect_equal("nested dynamic arrays",
                     polymarket::evm_abi_encode_call(
                         "k(uint256[][],address[])",
                         {EvmAbiValue::array({EvmAbiValue::array(binary_partition()), EvmAbiValue::array({}),
                                              EvmAbiValue::array({EvmAbiValue::uint256(3)})}),
                          EvmAbiValue::array({EvmAbiValue::address(kCtf), EvmAbiValue::address(kNegRiskAdapter)})}),
                     "0x5aa5c48d"
                     "0000000000000000000000000000000000000000000000000000000000000040"
                     "0000000000000000000000000000000000000000000000000000000000000180"
                     "0000000000000000000000000000000000000000000000000000000000000003"
                     "0000000000000000000000000000000000000000000000000000000000000060"
                     "00000000000000000000000000000000000000000000000000000000000000c0"
                     "00000000000000000000000000000000000000000000000000000000000000e0"
                     "0000000000000000000000000000000000000000000000000000000000000002"
                     "0000000000000000000000000000000000000000000000000000000000000001"
                     "0000000000000000000000000000000000000000000000000000000000000002"
                     "0000000000000000000000000000000000000000000000000000000000000000"
                     "0000000000000000000000000000000000000000000000000000000000000001"
                     "0000000000000000000000000000000000000000000000000000000000000003"
                     "0000000000000000000000000000000000000000000000000000000000000002"
                     "0000000000000000000000004d97dcd97ec945f40cf65f87097ace5ea0476045"
                     "000000000000000000000000d91e80cf2e7be2e162c6513ced06f1dd0da35296");
    }
} // namespace

int main()
{
    eth_abi_vectors();
    return check_support::finish("test_evm_abi_shapes");
}
