#pragma once

#include "check_support.hpp"
#include "evm_abi.hpp"
#include "order_signer.hpp"
#include "position_calls.hpp"
#include "position_client.hpp"
#include "../src/clob_client_test_fixture.hpp"

#include <functional>
#include <string>

// Shared fixtures for the position and Safe relayer tests. Vectors were
// generated with the official py-sdk using the EIP-155 example key 0x4646...46.
namespace position_test
{
    using namespace polymarket;
    using check_support::check;
    using check_support::expect_equal;
    using check_support::expect_throws;

    const std::string kKey = "4646464646464646464646464646464646464646464646464646464646464646";
    const std::string kWallet = "0x9d8A62f656a8d1615C1294fd71e9CFb3E4855A4F"; // EOA of kKey
    const std::string kCondition = "0x6b049bc3befa02c22d0cfaee77625fc94e9a6d571f00c015ba7a3b442e306fbc";
    const std::string kCondition2 = "0x" + std::string(62, 'c') + "00"; // V2: bytes31 + outcome byte
    const std::string kYes = "31064898417065273142871899984576329875203989058298067751883887309618656717886";
    const std::string kNo = "46464336901469148231749620229467543535010257111143284104831372917724022712604";
    const auto kContracts = PolymarketContracts::polygon_mainnet();

    // Safe of kKey and a redeem of kCondition through the collateral adapter.
    const std::string kSafe = "0xe909E402b7FA6E29D2f91b343B44Ed29C1d498Ed";
    const std::string kRedeemData =
        "0x01b7037c000000000000000000000000c011a7e12a19f7b1f670d46f03b03f3342e82dfb0000000000000000000000000000"
        "0000000000000000000000000000000000006b049bc3befa02c22d0cfaee77625fc94e9a6d571f00c015ba7a3b442e306fbc"
        "0000000000000000000000000000000000000000000000000000000000000080000000000000000000000000000000000000"
        "0000000000000000000000000002000000000000000000000000000000000000000000000000000000000000000100000000"
        "00000000000000000000000000000000000000000000000000000002";
    const std::string kSingleSignature =
        "0x305d34092be5431fc4f8a71861507000e8e2f9c58c991a206160cfa44765fe3067589adc5ee3eccee4007b23fcf26ce21f19"
        "eafb63c551255b7631eb8bb4ecb320";

    inline nlohmann::json gamma_market(const std::string &condition, const std::string &version = "v1",
                                       bool neg_risk = false)
    {
        nlohmann::json market = {{"id", "2959706"},
                                 {"conditionId", condition},
                                 {"version", version},
                                 {"negRisk", neg_risk},
                                 {"clobTokenIds", "[\"" + kYes + "\", \"" + kNo + "\"]"},
                                 {"positionIds", nlohmann::json::array({"100", "101"})}};
        return nlohmann::json::array({market});
    }

    // ---- scripted JSON-RPC node ----

    inline std::string json_rpc_reply(const clob_test::Request &request, const nlohmann::json &result)
    {
        const auto id = nlohmann::json::parse(request.body).at("id");
        return nlohmann::json{{"jsonrpc", "2.0"}, {"id", id}, {"result", result}}.dump();
    }

    // Queues one RPC response that asserts the method name.
    inline void expect_rpc(clob_test::LocalServer &node, const std::string &method,
                    std::function<nlohmann::json(const nlohmann::json &params)> result)
    {
        node.enqueue([method, result](const clob_test::Request &request)
                     {
            const auto body = nlohmann::json::parse(request.body);
            check(body.at("method") == method, "expected " + method + ", got " + body.at("method").dump());
            return json_rpc_reply(request, result(body.at("params"))); });
    }

    inline void expect_rpc(clob_test::LocalServer &node, const std::string &method, nlohmann::json result)
    {
        expect_rpc(node, method, [result](const nlohmann::json &) { return result; });
    }

    inline nlohmann::json receipt(const std::string &hash, bool success)
    {
        return {{"transactionHash", hash},
                {"blockHash", "0x" + std::string(64, 'b')},
                {"blockNumber", "0x10"},
                {"gasUsed", "0x5208"},
                {"status", success ? "0x1" : "0x0"},
                {"logs", nlohmann::json::array()}};
    }

    inline std::string balances_result(const std::string &yes, const std::string &no)
    {
        return to_hex(evm_abi_encode({EvmAbiValue::array({EvmAbiValue::uint256(yes), EvmAbiValue::uint256(no)})}));
    }
} // namespace position_test
