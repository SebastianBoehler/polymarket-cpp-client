#include "check_support.hpp"
#include "polymarket/evm_abi.hpp"
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

// Expected calldata comes from the official py-sdk golden tests
// (tests/unit/test_relayer_call_encoders_golden.py).

namespace
{
    using check_support::expect_equal;
    using check_support::expect_throws;
    using polymarket::EvmAbiValue;

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
        expect_throws<std::invalid_argument>("uint256 overflow by one", []
                      { (void)EvmAbiValue::uint256("115792089237316195423570985008687907853269984665640564039457584007913129639936"); });
        expect_throws<std::invalid_argument>("hex overflow", []
                      { (void)EvmAbiValue::uint256("0x1" + std::string(64, '0')); });
        expect_throws<std::invalid_argument>("uint8 overflow", []
                      { (void)EvmAbiValue::unsigned_integer("256", 8); });
        expect_throws<std::invalid_argument>("uint8 overflow from integer", []
                      { (void)EvmAbiValue::unsigned_integer(256, 8); });
        expect_throws<std::invalid_argument>("bad width", []
                      { (void)EvmAbiValue::unsigned_integer(1, 7); });
        expect_throws<std::invalid_argument>("negative", []
                      { (void)EvmAbiValue::uint256("-1"); });
        expect_throws<std::invalid_argument>("plus sign", []
                      { (void)EvmAbiValue::uint256("+1"); });
        expect_throws<std::invalid_argument>("fractional", []
                      { (void)EvmAbiValue::uint256("1.5"); });
        expect_throws<std::invalid_argument>("empty decimal", []
                      { (void)EvmAbiValue::uint256(""); });
        expect_throws<std::invalid_argument>("empty hex", []
                      { (void)EvmAbiValue::uint256("0x"); });
        expect_throws<std::invalid_argument>("bad hex digit", []
                      { (void)EvmAbiValue::uint256("0x1g"); });
        expect_throws<std::invalid_argument>("short address", []
                      { (void)EvmAbiValue::address("0x1234"); });
        expect_throws<std::invalid_argument>("long address", []
                      { (void)EvmAbiValue::address(kCtf + "00"); });
        expect_throws<std::invalid_argument>("odd-length address", []
                      { (void)EvmAbiValue::address(kCtf.substr(0, 41)); });
        expect_throws<std::invalid_argument>("bytes32 too short", []
                      { (void)EvmAbiValue::bytes32("0x" + std::string(62, '1')); });
        expect_throws<std::invalid_argument>("bytes31 given 32 bytes", []
                      { (void)EvmAbiValue::fixed_bytes(kCondition, 31); });
        expect_throws<std::invalid_argument>("bytes0", []
                      { (void)EvmAbiValue::fixed_bytes("0x", 0); });
        expect_throws<std::invalid_argument>("bytes odd hex", []
                      { (void)EvmAbiValue::bytes(std::string("0xabc")); });
    }
} // namespace

int main()
{
    official_golden_vectors();
    integer_parsing();
    rejections();
    return check_support::finish("test_evm_abi");
}
