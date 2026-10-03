#include "evm_abi.hpp"
#include <cstdint>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

// Expected calldata comes from the official py-sdk golden tests
// (tests/unit/test_relayer_call_encoders_golden.py) and from eth_abi 5.x for
// the shapes those tests do not cover.

namespace
{
    using polymarket::EvmAbiValue;

    int failures = 0;

    void expect_equal(const std::string &name, const std::string &actual, const std::string &expected)
    {
        if (actual == expected)
            return;
        ++failures;
        std::cerr << name << " mismatch\n"
                  << "  expected: " << expected << "\n"
                  << "  actual:   " << actual << "\n";
    }

    void expect_throws(const std::string &name, const std::function<void()> &action)
    {
        try
        {
            action();
        }
        catch (const std::invalid_argument &)
        {
            return;
        }
        catch (const std::exception &error)
        {
            ++failures;
            std::cerr << name << " threw the wrong exception type: " << error.what() << "\n";
            return;
        }
        ++failures;
        std::cerr << name << " did not throw\n";
    }

    const std::string kUsdc = "0x2791Bca1f2de4661ED88A30C99A7a9449Aa84174";
    const std::string kCtf = "0x4D97DCd97eC945f40cF65F87097ACe5EA0476045";
    const std::string kNegRiskAdapter = "0xd91E80cF2E7be2e162c6513ceD06f1dD0dA35296";
    const std::string kZero32 = "0x" + std::string(64, '0');
    const std::string kCondition = "0x" + std::string(64, '1');

    std::vector<EvmAbiValue> binary_partition()
    {
        return {EvmAbiValue::uint256(1), EvmAbiValue::uint256(2)};
    }

    void official_golden_vectors()
    {
        expect_equal("selector approve", polymarket::evm_function_selector("approve(address,uint256)"),
                     "0x095ea7b3");
        expect_equal("selector balanceOfBatch",
                     polymarket::evm_function_selector("balanceOfBatch(address[],uint256[])"),
                     "0x4e1273f4");

        expect_equal("approve max",
                     polymarket::evm_abi_encode_call(
                         "approve(address,uint256)",
                         {EvmAbiValue::address(kCtf),
                          EvmAbiValue::uint256("115792089237316195423570985008687907853269984665640564039457584007913129639935")}),
                     "0x095ea7b3"
                     "0000000000000000000000004d97dcd97ec945f40cf65f87097ace5ea0476045"
                     "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");

        expect_equal("transfer one unit",
                     polymarket::evm_abi_encode_call(
                         "transfer(address,uint256)",
                         {EvmAbiValue::address("0x000000000000000000000000000000000000dEaD"),
                          EvmAbiValue::uint256(1)}),
                     "0xa9059cbb"
                     "000000000000000000000000000000000000000000000000000000000000dead"
                     "0000000000000000000000000000000000000000000000000000000000000001");

        expect_equal("setApprovalForAll false",
                     polymarket::evm_abi_encode_call(
                         "setApprovalForAll(address,bool)",
                         {EvmAbiValue::address(kNegRiskAdapter), EvmAbiValue::boolean(false)}),
                     "0xa22cb465"
                     "000000000000000000000000d91e80cf2e7be2e162c6513ced06f1dd0da35296"
                     "0000000000000000000000000000000000000000000000000000000000000000");

        const std::string split_merge_body =
            "0000000000000000000000002791bca1f2de4661ed88a30c99a7a9449aa84174"
            "0000000000000000000000000000000000000000000000000000000000000000"
            "1111111111111111111111111111111111111111111111111111111111111111"
            "00000000000000000000000000000000000000000000000000000000000000a0"
            "00000000000000000000000000000000000000000000000000000000000f4240"
            "0000000000000000000000000000000000000000000000000000000000000002"
            "0000000000000000000000000000000000000000000000000000000000000001"
            "0000000000000000000000000000000000000000000000000000000000000002";
        const std::vector<EvmAbiValue> split_args = {
            EvmAbiValue::address(kUsdc), EvmAbiValue::bytes32(kZero32),
            EvmAbiValue::bytes32(kCondition), EvmAbiValue::array(binary_partition()),
            EvmAbiValue::uint256("1000000")};
        expect_equal("splitPosition",
                     polymarket::evm_abi_encode_call(
                         "splitPosition(address,bytes32,bytes32,uint256[],uint256)", split_args),
                     "0x72ce4275" + split_merge_body);
        expect_equal("mergePositions",
                     polymarket::evm_abi_encode_call(
                         "mergePositions(address,bytes32,bytes32,uint256[],uint256)", split_args),
                     "0x9e7212ad" + split_merge_body);

        expect_equal("redeemPositions",
                     polymarket::evm_abi_encode_call(
                         "redeemPositions(address,bytes32,bytes32,uint256[])",
                         {EvmAbiValue::address(kUsdc), EvmAbiValue::bytes32(kZero32),
                          EvmAbiValue::bytes32(kCondition), EvmAbiValue::array(binary_partition())}),
                     "0x01b7037c"
                     "0000000000000000000000002791bca1f2de4661ed88a30c99a7a9449aa84174"
                     "0000000000000000000000000000000000000000000000000000000000000000"
                     "1111111111111111111111111111111111111111111111111111111111111111"
                     "0000000000000000000000000000000000000000000000000000000000000080"
                     "0000000000000000000000000000000000000000000000000000000000000002"
                     "0000000000000000000000000000000000000000000000000000000000000001"
                     "0000000000000000000000000000000000000000000000000000000000000002");
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

    void integer_parsing()
    {
        const auto word = [](const EvmAbiValue &value)
        { return polymarket::evm_abi_encode_call("x()", {value}).substr(10); };
        expect_equal("decimal leading zeros", word(EvmAbiValue::uint256("000042")),
                     std::string(62, '0') + "2a");
        expect_equal("hex odd length", word(EvmAbiValue::uint256("0xabc")), std::string(61, '0') + "abc");
        expect_equal("uint64 max", word(EvmAbiValue::uint256(UINT64_MAX)),
                     std::string(48, '0') + "ffffffffffffffff");
        expect_equal("hex with leading zero digits beyond a word",
                     word(EvmAbiValue::uint256("0x00" + std::string(64, 'f'))), std::string(64, 'f'));
    }

    void rejections()
    {
        expect_throws("uint256 overflow by one", []
                      { (void)EvmAbiValue::uint256("115792089237316195423570985008687907853269984665640564039457584007913129639936"); });
        expect_throws("hex overflow", []
                      { (void)EvmAbiValue::uint256("0x1" + std::string(64, '0')); });
        expect_throws("uint8 overflow", []
                      { (void)EvmAbiValue::unsigned_integer("256", 8); });
        expect_throws("uint8 overflow from integer", []
                      { (void)EvmAbiValue::unsigned_integer(256, 8); });
        expect_throws("bad width", []
                      { (void)EvmAbiValue::unsigned_integer(1, 7); });
        expect_throws("negative", []
                      { (void)EvmAbiValue::uint256("-1"); });
        expect_throws("plus sign", []
                      { (void)EvmAbiValue::uint256("+1"); });
        expect_throws("fractional", []
                      { (void)EvmAbiValue::uint256("1.5"); });
        expect_throws("empty decimal", []
                      { (void)EvmAbiValue::uint256(""); });
        expect_throws("empty hex", []
                      { (void)EvmAbiValue::uint256("0x"); });
        expect_throws("bad hex digit", []
                      { (void)EvmAbiValue::uint256("0x1g"); });
        expect_throws("short address", []
                      { (void)EvmAbiValue::address("0x1234"); });
        expect_throws("long address", []
                      { (void)EvmAbiValue::address(kCtf + "00"); });
        expect_throws("odd-length address", []
                      { (void)EvmAbiValue::address(kCtf.substr(0, 41)); });
        expect_throws("bytes32 too short", []
                      { (void)EvmAbiValue::bytes32("0x" + std::string(62, '1')); });
        expect_throws("bytes31 given 32 bytes", []
                      { (void)EvmAbiValue::fixed_bytes(kCondition, 31); });
        expect_throws("bytes0", []
                      { (void)EvmAbiValue::fixed_bytes("0x", 0); });
        expect_throws("bytes odd hex", []
                      { (void)EvmAbiValue::bytes(std::string("0xabc")); });
    }
} // namespace

int main()
{
    official_golden_vectors();
    eth_abi_vectors();
    integer_parsing();
    rejections();
    if (failures != 0)
    {
        std::cerr << failures << " test_evm_abi check(s) failed\n";
        return 1;
    }
    std::cout << "test_evm_abi passed\n";
    return 0;
}
