// PositionClient EOA flows against a scripted RPC node and Gamma server.
#include "position_test_support.hpp"

using namespace position_test;

namespace
{
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

    PositionClientConfig eoa_config(const std::string &rpc_url, const std::string &gamma_url = "",
                                    const PolymarketContracts &contracts = kContracts)
    {
        PositionClientConfig config;
        config.private_key = kKey;
        config.rpc_url = rpc_url;
        if (!gamma_url.empty())
            config.gamma_api_url = gamma_url;
        config.contracts = contracts;
        config.rpc_timeout_ms = 5000;
        return config;
    }

    struct Harness
    {
        clob_test::LocalServer node;
        clob_test::LocalServer gamma;
        PositionClient client;

        Harness() : client(eoa_config(node.url(), gamma.url())) {}

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

    // bytes31 V2 ids, as in py-sdk test_relayer_position_workflows; Gamma gets the id as given.
    void v2_bytes31_condition_id()
    {
        Harness h;
        const std::string bytes31 = "0x01" + std::string(60, '4');
        h.market(bytes31, "v2");
        expect_transaction(h.node, kContracts.protocol_v2_router,
                           detail::router_split_call(kContracts.protocol_v2_router, bytes31, "5").data, true);
        (void)h.client.split_position(bytes31, "5");
        h.market(bytes31, "v2");
        expect_rpc(h.node, "eth_call", balances_result("100", "60"));
        expect_transaction(h.node, kContracts.protocol_v2_router,
                           detail::router_merge_call(kContracts.protocol_v2_router, bytes31, "60").data, false);
        (void)h.client.merge_positions(bytes31, "max");
        for (const auto &request : h.gamma.requests())
            check(request.target == "/markets?condition_ids=" + bytes31, "Gamma lookup target " + request.target);
        h.market(bytes31, "v2");
        expect_rpc(h.node, "eth_call", balances_result("1", "1"));
        h.market(bytes31, "v2");
        expect_throws<std::invalid_argument>("bytes31 and bytes32 forms of one condition", [&]
                                             { (void)h.client.merge_multiple_positions({{bytes31, "1"}, {bytes31 + "00", "1"}}); });
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

    void batch_failure_reports_submitted_hashes()
    {
        Harness h;
        h.market(kCondition);
        expect_rpc(h.node, "eth_call", balances_result("10", "20"));
        h.market(kCondition2, "v2");
        expect_rpc(h.node, "eth_call", balances_result("4", "4"));
        expect_transaction(h.node, kContracts.collateral_adapter,
                           detail::ctf_merge_positions_call(kContracts.collateral_adapter, kContracts.collateral_token,
                                                            kCondition, "10")
                               .data,
                           true);
        std::string first_hash;
        expect_rpc(h.node, "eth_getTransactionReceipt", [&first_hash](const nlohmann::json &params)
                   {
            first_hash = params.at(0).get<std::string>();
            return receipt(first_hash, true); });
        // The second call's gas estimate reverts.
        expect_rpc(h.node, "eth_getTransactionCount", "0x8");
        expect_rpc(h.node, "eth_gasPrice", "0x1");
        h.node.enqueue([](const clob_test::Request &request)
                       {
            const auto id = nlohmann::json::parse(request.body).at("id");
            return nlohmann::json{{"jsonrpc", "2.0"}, {"id", id},
                                  {"error", {{"code", 3}, {"message", "execution reverted: insufficient balance"}}}}.dump(); });
        try
        {
            (void)h.client.merge_multiple_positions({{kCondition, "max"}, {kCondition2, "2"}});
            check(false, "batch failure must throw");
        }
        catch (const PartialBatchError &error)
        {
            const std::string what = error.what();
            check(error.failed_call_index() == 1, "failed call index " + std::to_string(error.failed_call_index()));
            check(error.submitted_hashes() == std::vector<std::string>{first_hash},
                  "submitted hashes must list the mined first transaction");
            check(what.find(first_hash) != std::string::npos && what.find("call 1 of 2") != std::string::npos &&
                      what.find("insufficient balance") != std::string::npos,
                  "batch error message: " + what);
            expect_throws<std::runtime_error>("cause is the RPC error", [&]
                                              { std::rethrow_exception(error.cause()); });
        }
        check(h.rpc_count("eth_sendRawTransaction") == 1, "second transaction not sent");
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
                                             { PositionClient client(eoa_config("")); });
        auto contracts = kContracts;
        contracts.collateral_adapter = "0x1234";
        expect_throws<std::invalid_argument>("bad contracts", [&]
                                             { PositionClient client(eoa_config("http://127.0.0.1:1", "", contracts)); });
    }
} // namespace

int main()
{
    split_flow();
    v2_bytes31_condition_id();
    wrong_chain_sends_nothing();
    estimate_failure_sends_nothing();
    batch_failure_reports_submitted_hashes();
    merge_flows();
    redeem_flows();
    config_validation();
    return check_support::finish("test_position_client");
}
