#include "evm_abi.hpp"
#include "order_signer.hpp"
#include "position_calls.hpp"
#include "position_client.hpp"
#include "../src/clob_client_test_fixture.hpp"

#include <functional>
#include <iostream>
#include <string>
#include <vector>

// Calldata vectors were generated with the official py-sdk call builders
// (polymarket._internal.actions.relayer.calls).

using namespace polymarket;

namespace
{
    int failures = 0;

    void check(bool condition, const std::string &message)
    {
        if (condition)
            return;
        ++failures;
        std::cerr << message << "\n";
    }

    void expect_equal(const std::string &name, const std::string &actual, const std::string &expected)
    {
        check(actual == expected, name + " mismatch\n  expected: " + expected + "\n  actual:   " + actual);
    }

    template <typename Error>
    void expect_throws(const std::string &name, const std::function<void()> &action)
    {
        try
        {
            action();
        }
        catch (const Error &)
        {
            return;
        }
        catch (const std::exception &error)
        {
            check(false, name + " threw the wrong exception type: " + error.what());
            return;
        }
        check(false, name + " did not throw");
    }

    const std::string kKey = "4646464646464646464646464646464646464646464646464646464646464646";
    const std::string kWallet = "0x9d8A62f656a8d1615C1294fd71e9CFb3E4855A4F";
    const std::string kCondition = "0x6b049bc3befa02c22d0cfaee77625fc94e9a6d571f00c015ba7a3b442e306fbc";
    const std::string kCondition2 = "0x" + std::string(62, 'c') + "00"; // V2: bytes31 + outcome byte
    const std::string kYes = "31064898417065273142871899984576329875203989058298067751883887309618656717886";
    const std::string kNo = "46464336901469148231749620229467543535010257111143284104831372917724022712604";
    const auto kContracts = PolymarketContracts::polygon_mainnet();

    nlohmann::json gamma_market(const std::string &condition, const std::string &version, bool neg_risk)
    {
        nlohmann::json market = {{"id", "2959706"},
                                 {"conditionId", condition},
                                 {"version", version},
                                 {"negRisk", neg_risk},
                                 {"clobTokenIds", "[\"" + kYes + "\", \"" + kNo + "\"]"},
                                 {"positionIds", nlohmann::json::array({"100", "101"})}};
        return nlohmann::json::array({market});
    }

    void calldata_vectors()
    {
        expect_equal("neg-risk adapter split",
                     detail::ctf_split_position_call(kContracts.neg_risk_collateral_adapter,
                                                     kContracts.collateral_token, kCondition, "2500000")
                         .data,
                     "0x72ce4275000000000000000000000000c011a7e12a19f7b1f670d46f03b03f3342e82dfb0000000000000000"
                     "0000000000000000000000000000000000000000000000006b049bc3befa02c22d0cfaee77625fc94e9a6d571f00"
                     "c015ba7a3b442e306fbc00000000000000000000000000000000000000000000000000000000000000a000000000"
                     "000000000000000000000000000000000000000000000000002625a0000000000000000000000000000000000000"
                     "0000000000000000000000000002000000000000000000000000000000000000000000000000000000000000000100"
                     "00000000000000000000000000000000000000000000000000000000000002");
        expect_equal("adapter redeem",
                     detail::ctf_redeem_positions_call(kContracts.collateral_adapter, kContracts.collateral_token,
                                                       kCondition)
                         .data,
                     "0x01b7037c000000000000000000000000c011a7e12a19f7b1f670d46f03b03f3342e82dfb0000000000000000"
                     "0000000000000000000000000000000000000000000000006b049bc3befa02c22d0cfaee77625fc94e9a6d571f00"
                     "c015ba7a3b442e306fbc000000000000000000000000000000000000000000000000000000000000008000000000"
                     "0000000000000000000000000000000000000000000000000000000200000000000000000000000000000000000000"
                     "000000000000000000000000010000000000000000000000000000000000000000000000000000000000000002");
        std::string v2_condition = "0x";
        for (int i = 0; i < 31; ++i)
            v2_condition += "ab";
        expect_equal("router split",
                     detail::router_split_call(kContracts.protocol_v2_router, v2_condition + "01", "1000000").data,
                     "0x82d3b9f1ababababababababababababababababababababababababababababababab00000000000000000000"
                     "00000000000000000000000000000000000000000f4240");
        expect_equal("router merge",
                     detail::router_merge_call(kContracts.protocol_v2_router, v2_condition, "7").data,
                     "0x5b63685eababababababababababababababababababababababababababababababab00000000000000000000"
                     "0000000000000000000000000000000000000000000007");
        expect_equal("router redeem",
                     detail::router_redeem_call(kContracts.protocol_v2_router, v2_condition + "01", 1, "61333300").data,
                     "0xd217a3ccababababababababababababababababababababababababababababababab00000000000000000000"
                     "0000000000000000000000000000000000000000000001000000000000000000000000000000000000000000000000"
                     "0000000003a7df34");

        MarketPositionContext market;
        market.token_ids = {kYes, kNo};
        market.position_token_contract = kContracts.conditional_tokens;
        const auto balance_call = detail::balance_of_batch_call(market, "0xa8Beb4744dc06f51355b92baa9a57dd7F127D006");
        check(balance_call.to == kContracts.conditional_tokens, "balanceOfBatch target");
        expect_equal("balanceOfBatch", balance_call.data,
                     "0x4e1273f4000000000000000000000000000000000000000000000000000000000000004000000000000000000000"
                     "000000000000000000000000000000000000000000a0000000000000000000000000000000000000000000000000"
                     "0000000000000002000000000000000000000000a8beb4744dc06f51355b92baa9a57dd7f127d006000000000000"
                     "000000000000a8beb4744dc06f51355b92baa9a57dd7f127d00600000000000000000000000000000000000000000000"
                     "0000000000000000000244ae1c02ca63bf316480f7afdc21d071e80fb84e921c257d5eeea383ca1e2c3e66b9e1fa39"
                     "b50d28bacceebb70a50f13bd5a77c61f78457a189487a0796e291c");

        expect_throws<std::invalid_argument>("v2 bytes32 with non-outcome last byte", [&]
                                             { (void)detail::v2_condition_id_bytes31(v2_condition + "02"); });
        expect_throws<std::invalid_argument>("short condition id", []
                                             { (void)detail::normalize_condition_id("0x1234"); });
        expect_throws<std::invalid_argument>("unprefixed condition id", []
                                             { (void)detail::normalize_condition_id(std::string(64, '1')); });
    }

    void gamma_parsing()
    {
        const auto ctf = detail::market_context_from_gamma(gamma_market(kCondition, "v1", false), kCondition, kContracts);
        check(ctf.protocol == MarketProtocol::Ctf && !ctf.neg_risk && ctf.market_id == "2959706" &&
                  ctf.token_ids[0] == kYes && ctf.token_ids[1] == kNo &&
                  ctf.position_token_contract == kContracts.conditional_tokens &&
                  ctf.operator_contract == kContracts.collateral_adapter,
              "v1 market context");

        const auto upper = detail::market_context_from_gamma(
            gamma_market("0x6B049BC3BEFA02C22D0CFAEE77625FC94E9A6D571F00C015BA7A3B442E306FBC", "v1", true),
            kCondition, kContracts);
        check(upper.neg_risk && upper.position_token_contract == kContracts.neg_risk_adapter &&
                  upper.operator_contract == kContracts.neg_risk_collateral_adapter,
              "neg-risk market context with uppercase condition id");

        const auto v2 = detail::market_context_from_gamma(gamma_market(kCondition2, "v2", false), kCondition2, kContracts);
        check(v2.protocol == MarketProtocol::V2 && v2.token_ids[0] == "100" && v2.token_ids[1] == "101" &&
                  v2.position_token_contract == kContracts.position_manager &&
                  v2.operator_contract == kContracts.protocol_v2_router,
              "v2 market context");

        expect_throws<std::invalid_argument>("no market", [&]
                                             { (void)detail::market_context_from_gamma(nlohmann::json::array(), kCondition, kContracts); });
        const auto rejects = [&](const std::string &name, const std::function<void(nlohmann::json &)> &mutate)
        {
            auto markets = gamma_market(kCondition, "v1", false);
            mutate(markets[0]);
            expect_throws<std::runtime_error>(name, [&]
                                              { (void)detail::market_context_from_gamma(markets, kCondition, kContracts); });
        };
        rejects("different condition", [](auto &m)
                { m["conditionId"] = kCondition2; });
        rejects("missing version", [](auto &m)
                { m.erase("version"); });
        rejects("unknown version", [](auto &m)
                { m["version"] = "v3"; });
        rejects("missing negRisk", [](auto &m)
                { m.erase("negRisk"); });
        rejects("three token ids", [](auto &m)
                { m["clobTokenIds"] = nlohmann::json::array({"1", "2", "3"}); });
        rejects("non-numeric token id", [](auto &m)
                { m["clobTokenIds"] = nlohmann::json::array({"1", "x"}); });
        rejects("unparseable token list", [](auto &m)
                { m["clobTokenIds"] = "[1,"; });
        expect_throws<std::runtime_error>("two markets", [&]
                                          {
            auto markets = gamma_market(kCondition, "v1", false);
            markets.push_back(markets[0]);
            (void)detail::market_context_from_gamma(markets, kCondition, kContracts); });
    }

    void merge_amounts()
    {
        expect_equal("max takes the smaller side", detail::resolve_merge_amount(kCondition, {"5000000", "3000000"}, "max"),
                     "3000000");
        expect_equal("explicit amount", detail::resolve_merge_amount(kCondition, {"5000000", "3000000"}, "2000000"),
                     "2000000");
        expect_equal("uint256-scale balances",
                     detail::resolve_merge_amount(kCondition, {"100000000000000000000000000000", "99999999999999999999999999999"}, "max"),
                     "99999999999999999999999999999");
        expect_throws<std::invalid_argument>("amount above max", []
                                             { (void)detail::resolve_merge_amount(kCondition, {"5", "3"}, "4"); });
        expect_throws<std::invalid_argument>("one side empty", []
                                             { (void)detail::resolve_merge_amount(kCondition, {"5", "0"}, "max"); });
        expect_throws<std::invalid_argument>("zero amount", []
                                             { (void)detail::resolve_merge_amount(kCondition, {"5", "3"}, "0"); });
        expect_throws<std::invalid_argument>("decimal amount", []
                                             { (void)detail::resolve_merge_amount(kCondition, {"5", "3"}, "1.5"); });
    }

    // ---- client flows against a scripted RPC node and Gamma server ----

    std::string json_rpc_reply(const clob_test::Request &request, const nlohmann::json &result)
    {
        const auto id = nlohmann::json::parse(request.body).at("id");
        return nlohmann::json{{"jsonrpc", "2.0"}, {"id", id}, {"result", result}}.dump();
    }

    // Queues one RPC response that asserts the method name.
    void expect_rpc(clob_test::LocalServer &node, const std::string &method,
                    std::function<nlohmann::json(const nlohmann::json &params)> result)
    {
        node.enqueue([method, result](const clob_test::Request &request)
                     {
            const auto body = nlohmann::json::parse(request.body);
            check(body.at("method") == method, "expected " + method + ", got " + body.at("method").dump());
            return json_rpc_reply(request, result(body.at("params"))); });
    }

    void expect_rpc(clob_test::LocalServer &node, const std::string &method, nlohmann::json result)
    {
        expect_rpc(node, method, [result](const nlohmann::json &) { return result; });
    }

    void expect_transaction(clob_test::LocalServer &node, const std::string &to, const std::string &data,
                            bool first_transaction)
    {
        if (first_transaction)
            expect_rpc(node, "eth_chainId", "0x89");
        expect_rpc(node, "eth_getTransactionCount", [](const nlohmann::json &params)
                   {
            check(params == nlohmann::json::array({kWallet, "pending"}), "nonce params " + params.dump());
            return nlohmann::json("0x7"); });
        expect_rpc(node, "eth_gasPrice", "0x6fc23ac00");
        expect_rpc(node, "eth_estimateGas", [to, data](const nlohmann::json &params)
                   {
            const auto &call = params.at(0);
            check(call.at("to") == to && call.at("from") == kWallet && call.at("value") == "0x0",
                  "estimate call " + call.dump());
            check(call.at("data") == data, "estimate calldata " + call.at("data").dump() + " expected " + data);
            return nlohmann::json("0x3d090"); });
        // Answer with keccak256(raw), as a real node would.
        expect_rpc(node, "eth_sendRawTransaction", [](const nlohmann::json &params)
                   { return nlohmann::json(to_hex(keccak256(from_hex(params.at(0).get<std::string>())))); });
    }

    nlohmann::json receipt(const std::string &hash, bool success)
    {
        return {{"transactionHash", hash},
                {"blockHash", "0x" + std::string(64, 'b')},
                {"blockNumber", "0x10"},
                {"gasUsed", "0x5208"},
                {"status", success ? "0x1" : "0x0"},
                {"logs", nlohmann::json::array()}};
    }

    std::string balances_result(const std::string &yes, const std::string &no)
    {
        return to_hex(evm_abi_encode({EvmAbiValue::array({EvmAbiValue::uint256(yes), EvmAbiValue::uint256(no)})}));
    }

    struct Harness
    {
        clob_test::LocalServer node;
        clob_test::LocalServer gamma;
        PositionClient client;

        Harness() : client(PositionClientConfig{kKey, node.url(), gamma.url(), kContracts, 5000}) {}

        void market(const std::string &condition, const std::string &version = "v1", bool neg_risk = false)
        {
            gamma.enqueue(gamma_market(condition, version, neg_risk).dump());
        }

        size_t rpc_count(const std::string &method) const
        {
            size_t count = 0;
            for (const auto &request : node.requests())
                count += nlohmann::json::parse(request.body).at("method") == method;
            return count;
        }
    };

    const auto kFast = std::chrono::milliseconds(10);

    void split_flow()
    {
        Harness h;
        check(h.client.wallet_address() == kWallet, "wallet address");
        h.market(kCondition);
        const auto data = detail::ctf_split_position_call(kContracts.collateral_adapter, kContracts.collateral_token,
                                                          kCondition, "1000000")
                              .data;
        expect_transaction(h.node, kContracts.collateral_adapter, data, true);
        const auto handle = h.client.split_position(kCondition, "1000000");
        check(handle.transaction_hashes().size() == 1 && handle.transaction_hash().size() == 66, "split handle");

        const auto gamma_requests = h.gamma.requests();
        check(gamma_requests.size() == 1 && gamma_requests[0].target == "/markets?condition_ids=" + kCondition,
              "split gamma query " + (gamma_requests.empty() ? "" : gamma_requests[0].target));

        expect_rpc(h.node, "eth_getTransactionReceipt", nlohmann::json());
        expect_rpc(h.node, "eth_getTransactionReceipt", receipt(handle.transaction_hash(), true));
        const auto outcome = handle.wait(std::chrono::seconds(5), kFast);
        check(outcome.transaction_hash == handle.transaction_hash() && outcome.receipt.success, "split outcome");

        expect_rpc(h.node, "eth_getTransactionReceipt", receipt(handle.transaction_hash(), false));
        expect_throws<TransactionRevertedError>("reverted wait", [&]
                                                { (void)handle.wait(std::chrono::seconds(5), kFast); });
        for (int i = 0; i < 3; ++i)
            expect_rpc(h.node, "eth_getTransactionReceipt", nlohmann::json());
        expect_throws<TransactionTimeoutError>("wait timeout", [&]
                                               { (void)handle.wait(std::chrono::milliseconds(25), kFast); });

        expect_throws<std::invalid_argument>("zero split", [&]
                                             { (void)h.client.split_position(kCondition, "0"); });
    }

    void wrong_chain_sends_nothing()
    {
        Harness h;
        h.market(kCondition);
        expect_rpc(h.node, "eth_chainId", "0x13882");
        expect_throws<std::runtime_error>("wrong chain", [&]
                                          { (void)h.client.split_position(kCondition, "1"); });
        check(h.rpc_count("eth_sendRawTransaction") == 0, "nothing sent on wrong chain");
    }

    void estimate_failure_sends_nothing()
    {
        Harness h;
        h.market(kCondition);
        expect_rpc(h.node, "eth_chainId", "0x89");
        expect_rpc(h.node, "eth_getTransactionCount", "0x0");
        expect_rpc(h.node, "eth_gasPrice", "0x1");
        h.node.enqueue([](const clob_test::Request &request)
                       {
            const auto id = nlohmann::json::parse(request.body).at("id");
            return nlohmann::json{{"jsonrpc", "2.0"}, {"id", id},
                                  {"error", {{"code", 3}, {"message", "execution reverted: ERC20: insufficient allowance"}}}}.dump(); });
        try
        {
            (void)h.client.split_position(kCondition, "1");
            check(false, "estimate failure must throw");
        }
        catch (const std::runtime_error &error)
        {
            check(std::string(error.what()).find("insufficient allowance") != std::string::npos,
                  std::string("estimate error must surface the revert reason: ") + error.what());
        }
        check(h.rpc_count("eth_sendRawTransaction") == 0, "nothing sent after failed estimate");
    }

    void merge_flows()
    {
        Harness h;
        h.market(kCondition, "v1", true);
        expect_rpc(h.node, "eth_call", [](const nlohmann::json &params)
                   {
            check(params.at(0).at("to") == kContracts.neg_risk_adapter, "neg-risk balances read the adapter");
            check(params.at(1) == "latest", "balance block tag");
            return nlohmann::json(balances_result("5000000", "3000000")); });
        expect_transaction(h.node, kContracts.neg_risk_collateral_adapter,
                           detail::ctf_merge_positions_call(kContracts.neg_risk_collateral_adapter,
                                                            kContracts.collateral_token, kCondition, "3000000")
                               .data,
                           true);
        (void)h.client.merge_positions(kCondition, "max");

        // Two conditions: the first transaction must be mined before the second is sent.
        h.market(kCondition);
        expect_rpc(h.node, "eth_call", balances_result("10", "20"));
        h.market(kCondition2, "v2");
        expect_rpc(h.node, "eth_call", balances_result("4", "4"));
        expect_transaction(h.node, kContracts.collateral_adapter,
                           detail::ctf_merge_positions_call(kContracts.collateral_adapter, kContracts.collateral_token,
                                                            kCondition, "10")
                               .data,
                           false);
        std::string first_hash;
        expect_rpc(h.node, "eth_getTransactionReceipt", [&first_hash](const nlohmann::json &params)
                   {
            first_hash = params.at(0).get<std::string>();
            return receipt(first_hash, true); });
        expect_transaction(h.node, kContracts.protocol_v2_router,
                           detail::router_merge_call(kContracts.protocol_v2_router, kCondition2, "2").data, false);
        const auto handle = h.client.merge_multiple_positions({{kCondition, "max"}, {kCondition2, "2"}});
        check(handle.transaction_hashes().size() == 2 && handle.transaction_hashes()[0] == first_hash &&
                  handle.transaction_hash() != first_hash,
              "batch handle tracks both transactions");

        expect_throws<std::invalid_argument>("duplicate conditions", [&]
                                             { (void)h.client.merge_multiple_positions(
                                                   {{kCondition, "max"}, {"0x6B049BC3BEFA02C22D0CFAEE77625FC94E9A6D571F00C015BA7A3B442E306FBC", "1"}}); });
        expect_throws<std::invalid_argument>("empty batch", [&]
                                             { (void)h.client.merge_multiple_positions({}); });
    }

    void redeem_flows()
    {
        Harness h;
        h.market(kCondition);
        expect_rpc(h.node, "eth_call", balances_result("0", "61333300"));
        expect_transaction(h.node, kContracts.collateral_adapter,
                           detail::ctf_redeem_positions_call(kContracts.collateral_adapter, kContracts.collateral_token,
                                                             kCondition)
                               .data,
                           true);
        (void)h.client.redeem_positions(kCondition);
        const auto gamma_requests = h.gamma.requests();
        check(!gamma_requests.empty() && gamma_requests.back().target.ends_with("&closed=true"),
              "redeem looks up closed markets");

        h.market(kCondition);
        expect_rpc(h.node, "eth_call", balances_result("0", "0"));
        const auto sent_before = h.rpc_count("eth_sendRawTransaction");
        expect_throws<std::invalid_argument>("nothing to redeem", [&]
                                             { (void)h.client.redeem_positions(kCondition); });
        check(h.rpc_count("eth_sendRawTransaction") == sent_before, "nothing sent without a balance");

        // V2: one redeem per outcome with a balance.
        h.market(kCondition2, "v2");
        expect_rpc(h.node, "eth_call", balances_result("0", "9"));
        expect_transaction(h.node, kContracts.protocol_v2_router,
                           detail::router_redeem_call(kContracts.protocol_v2_router, kCondition2, 1, "9").data, false);
        check(h.client.redeem_positions(kCondition2).transaction_hashes().size() == 1, "v2 redeem of one outcome");
    }

    void config_validation()
    {
        expect_throws<std::invalid_argument>("missing rpc url", []
                                             { PositionClient client(PositionClientConfig{kKey, ""}); });
        auto contracts = kContracts;
        contracts.collateral_adapter = "0x1234";
        expect_throws<std::invalid_argument>("bad contracts", [&]
                                             { PositionClient client(PositionClientConfig{kKey, "http://127.0.0.1:1", "", contracts}); });
    }
} // namespace

int main()
{
    calldata_vectors();
    gamma_parsing();
    merge_amounts();
    split_flow();
    wrong_chain_sends_nothing();
    estimate_failure_sends_nothing();
    merge_flows();
    redeem_flows();
    config_validation();
    if (failures != 0)
    {
        std::cerr << failures << " test_position_client check(s) failed\n";
        return 1;
    }
    std::cout << "test_position_client passed\n";
    return 0;
}
