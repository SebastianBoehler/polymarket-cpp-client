// PositionClient trading approvals, approvals and transfers against scripted
// RPC node and relayer servers, for EOA and Safe wallets.
#include "approval_calls.hpp"
#include "position_test_support.hpp"
#include "safe_relayer.hpp"

using namespace position_test;

namespace
{
    const std::string recipient = "0x000000000000000000000000000000000000dEaD";
    const auto required_set = required_trading_approvals(kContracts);

    std::string word(const std::string &value)
    {
        return to_hex(evm_abi_encode({EvmAbiValue::uint256(value)}));
    }

    // Answers the 17 approval checks for `owner`. Results are given in
    // required order: ERC-20 allowances, then isApprovedForAll flags.
    void expect_approval_reads(clob_test::LocalServer &node, const std::string &owner,
                               const std::vector<std::string> &results)
    {
        const auto checks = detail::trading_approval_check_calls(required_set, owner);
        for (size_t i = 0; i < checks.size(); ++i)
        {
            const auto &expected = checks[i];
            const auto &result = results.at(i);
            expect_rpc(node, "eth_call",
                       [expected, result, i](const nlohmann::json &params)
                       {
                           check(params.at(0).at("to") == expected.to &&
                                     params.at(0).at("data") == expected.data &&
                                     params.at(1) == "latest",
                                 "approval check " + std::to_string(i) + " " + params.dump());
                           return nlohmann::json(result);
                       });
        }
    }

    std::vector<std::string> all_approved()
    {
        std::vector<std::string> results(required_set.erc20.size(), word(max_uint256));
        results.resize(required_set.erc20.size() + required_set.erc1155.size(), word("1"));
        return results;
    }

    void expect_transaction(clob_test::LocalServer &node, const ContractCall &call,
                            bool first_transaction)
    {
        if (first_transaction) expect_rpc(node, "eth_chainId", "0x89");
        expect_rpc(node, "eth_getTransactionCount", "0x7");
        expect_rpc(node, "eth_gasPrice", "0x6fc23ac00");
        expect_rpc(node, "eth_estimateGas",
                   [call](const nlohmann::json &params)
                   {
                       const auto &request = params.at(0);
                       check(request.at("to") == call.to && request.at("from") == kWallet &&
                                 request.at("data") == call.data,
                             "estimate call " + request.dump() + " expected data " + call.data);
                       return nlohmann::json("0x3d090");
                   });
        expect_rpc(node, "eth_sendRawTransaction",
                   [](const nlohmann::json &params)
                   {
                       return nlohmann::json(
                           to_hex(keccak256(from_hex(params.at(0).get<std::string>()))));
                   });
    }

    void expect_mined(clob_test::LocalServer &node)
    {
        expect_rpc(node, "eth_getTransactionReceipt", [](const nlohmann::json &params)
                   { return receipt(params.at(0).get<std::string>(), true); });
    }

    PositionClientConfig base_config(const std::string &node_url)
    {
        PositionClientConfig config;
        config.private_key = kKey;
        config.rpc_url = node_url;
        config.rpc_timeout_ms = 5000;
        return config;
    }

    size_t count(const clob_test::LocalServer &node, const std::string &method)
    {
        size_t n = 0;
        for (const auto &request : node.requests())
            n += nlohmann::json::parse(request.body).at("method") == method;
        return n;
    }

    void state_reads()
    {
        clob_test::LocalServer node;
        PositionClient client(base_config(node.url()));

        expect_approval_reads(node, kWallet, all_approved());
        auto state = client.get_trading_approvals_state();
        check(state.is_fully_approved && state.missing.empty(), "fully approved EOA");

        auto results = all_approved();
        results[5] = word("5");  // exchange_v3 allowance below max
        results[16] = word("0"); // position_manager -> auto_redeem_operator
        expect_approval_reads(node, recipient, results);
        state = client.get_trading_approvals_state(recipient);
        check(!state.is_fully_approved && state.missing.erc20.size() == 1 &&
                  state.missing.erc20[0].spender == kContracts.exchange_v3 &&
                  state.missing.erc1155.size() == 1 &&
                  state.missing.erc1155[0].token_address == kContracts.position_manager &&
                  state.missing.erc1155[0].operator_address == kContracts.auto_redeem_operator,
              "missing approvals for another wallet");
        check(count(node, "eth_sendRawTransaction") == 0, "reads send nothing");

        expect_throws<std::invalid_argument>(
            "invalid wallet",
            [&] { (void)client.get_trading_approvals_state(std::string("0x1234")); });
    }

    void eoa_setup()
    {
        clob_test::LocalServer node;
        PositionClient client(base_config(node.url()));

        expect_approval_reads(node, kWallet, all_approved());
        check(!client.setup_trading_approvals().has_value(), "nothing to set up");
        check(count(node, "eth_sendRawTransaction") == 0 && count(node, "eth_chainId") == 0,
              "fully approved wallet submits nothing");

        auto results = all_approved();
        results[1] = word("0"); // neg_risk_exchange allowance
        results[7] = word("0"); // CTF -> standard_exchange
        expect_approval_reads(node, kWallet, results);
        const auto approve = detail::erc20_approve_call(kContracts.collateral_token,
                                                        kContracts.neg_risk_exchange, max_uint256);
        const auto operator_call = detail::erc1155_set_approval_for_all_call(
            kContracts.conditional_tokens, kContracts.standard_exchange, true);
        expect_transaction(node, approve, true);
        expect_mined(node); // the approve lands before the next is sent
        expect_transaction(node, operator_call, false);
        expect_mined(node);
        const auto outcome = client.setup_trading_approvals(std::chrono::seconds(5));
        check(outcome.has_value() && outcome->receipt.success && outcome->transaction_id.empty(),
              "EOA setup waits for the last transaction");
        check(count(node, "eth_sendRawTransaction") == 2, "one transaction per missing approval");
    }

    void eoa_single_calls()
    {
        clob_test::LocalServer node;
        PositionClient client(base_config(node.url()));
        const auto &pusd = kContracts.collateral_token;

        expect_transaction(
            node, detail::erc20_approve_call(pusd, kContracts.exchange_v3, max_uint256), true);
        auto handle = client.approve_erc20(pusd, kContracts.exchange_v3, "max");
        check(handle.transaction_hashes().size() == 1, "approve_erc20 handle");

        expect_transaction(node, detail::erc20_approve_call(pusd, kContracts.exchange_v3, "0"),
                           false);
        (void)client.approve_erc20(pusd, kContracts.exchange_v3, "0");

        expect_transaction(node,
                           detail::erc1155_set_approval_for_all_call(
                               kContracts.position_manager, kContracts.protocol_v2_router, false),
                           false);
        (void)client.approve_erc1155_for_all(kContracts.position_manager,
                                             kContracts.protocol_v2_router, false);

        expect_transaction(node, detail::erc20_transfer_call(pusd, recipient, "1500000"), false);
        handle = client.transfer_erc20(pusd, recipient, "1500000");
        expect_mined(node);
        check(handle.wait(std::chrono::seconds(5), std::chrono::milliseconds(5)).receipt.success,
              "transfer mined");

        expect_throws<std::invalid_argument>(
            "zero transfer", [&] { (void)client.transfer_erc20(pusd, recipient, "0"); });
        expect_throws<std::invalid_argument>("bad recipient", [&]
                                             { (void)client.transfer_erc20(pusd, "dead", "1"); });
        expect_throws<std::invalid_argument>("bad spender",
                                             [&] { (void)client.approve_erc20(pusd, "", "max"); });
        expect_throws<std::invalid_argument>(
            "bad approval amount", [&] { (void)client.approve_erc20(pusd, recipient, "1.5"); });
        expect_throws<std::invalid_argument>(
            "bad operator",
            [&] { (void)client.approve_erc1155_for_all(kContracts.conditional_tokens, "0x"); });
        check(count(node, "eth_sendRawTransaction") == 4, "invalid input sends nothing");
    }

    void safe_setup_is_one_multisend()
    {
        clob_test::LocalServer node;
        clob_test::LocalServer relayer;
        auto config = base_config(node.url());
        config.wallet_type = SignatureType::POLY_GNOSIS_SAFE;
        config.relayer_url = relayer.url();
        config.relayer_api_key = "relayer-key";
        PositionClient client(config);

        auto results = all_approved();
        results[0] = word("0");
        results[4] = word("0");
        results[14] = word("0");
        expect_approval_reads(node, kSafe, results);
        expect_rpc(node, "eth_chainId", "0x89");
        relayer.enqueue(nlohmann::json{{"address", kSafe}, {"nonce", "5"}}.dump());
        const auto expected = detail::resolve_safe_call(
            {detail::erc20_approve_call(kContracts.collateral_token, kContracts.standard_exchange,
                                        max_uint256),
             detail::erc20_approve_call(kContracts.collateral_token, kContracts.protocol_v2_router,
                                        max_uint256),
             detail::erc1155_set_approval_for_all_call(kContracts.position_manager,
                                                       kContracts.protocol_v2_router, true)},
            kContracts);
        relayer.enqueue(
            [expected](const clob_test::Request &request)
            {
                const auto body = nlohmann::json::parse(request.body);
                check(request.target == "/submit" && body.at("to") == kContracts.safe_multisend &&
                          body.at("data") == expected.call.data &&
                          body.at("proxyWallet") == kSafe &&
                          body.at("signatureParams").at("operation") == "1" &&
                          body.at("metadata") == "Trading setup approvals",
                      "setup submit " + body.dump());
                return nlohmann::json{{"transactionID", "tx-9"}, {"state", "STATE_NEW"}}.dump();
            });
        const std::string mined = "0x" + std::string(64, 'e');
        relayer.enqueue(nlohmann::json{
            {"transaction_id", "tx-9"},
            {"transaction_hash", mined},
            {"state", "STATE_CONFIRMED"},
            {"error_msg",
             nullptr}}.dump());
        expect_mined(node);
        const auto outcome = client.setup_trading_approvals(std::chrono::seconds(5));
        check(outcome.has_value() && outcome->transaction_id == "tx-9" &&
                  outcome->transaction_hash == mined,
              "Safe setup outcome");
        size_t submits = 0;
        for (const auto &request : relayer.requests())
            submits += request.target == "/submit";
        check(submits == 1, "one relayer submission for every missing approval");
    }
} // namespace

int main()
{
    state_reads();
    eoa_setup();
    eoa_single_calls();
    safe_setup_is_one_multisend();
    return check_support::finish("test_trading_approvals");
}
