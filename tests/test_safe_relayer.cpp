#include "evm_abi.hpp"
#include "order_signer.hpp"
#include "position_calls.hpp"
#include "position_client.hpp"
#include "safe_relayer.hpp"
#include "../src/clob_client_test_fixture.hpp"

#include <functional>
#include <iostream>
#include <string>
#include <vector>

// Vectors were generated with the official py-sdk: derive_safe_wallet_address,
// encode_safe_multisend_call, sign_safe_transaction and build_safe_payload,
// using the EIP-155 example key 0x4646...46.

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
    const std::string kSigner = "0x9d8A62f656a8d1615C1294fd71e9CFb3E4855A4F";
    const std::string kSafe = "0xe909E402b7FA6E29D2f91b343B44Ed29C1d498Ed";
    const std::string kCondition = "0x6b049bc3befa02c22d0cfaee77625fc94e9a6d571f00c015ba7a3b442e306fbc";
    const std::string kCondition2 = "0x" + std::string(64, 'd');
    const auto kContracts = PolymarketContracts::polygon_mainnet();

    const std::string kRedeemData =
        "0x01b7037c000000000000000000000000c011a7e12a19f7b1f670d46f03b03f3342e82dfb0000000000000000000000000000"
        "0000000000000000000000000000000000006b049bc3befa02c22d0cfaee77625fc94e9a6d571f00c015ba7a3b442e306fbc"
        "0000000000000000000000000000000000000000000000000000000000000080000000000000000000000000000000000000"
        "0000000000000000000000000002000000000000000000000000000000000000000000000000000000000000000100000000"
        "00000000000000000000000000000000000000000000000000000002";
    const std::string kSingleSignature =
        "0x305d34092be5431fc4f8a71861507000e8e2f9c58c991a206160cfa44765fe3067589adc5ee3eccee4007b23fcf26ce21f19"
        "eafb63c551255b7631eb8bb4ecb320";

    std::vector<ContractCall> multisend_calls()
    {
        return {{kContracts.collateral_adapter, kRedeemData, "0"},
                {"0x000000000000000000000000000000000000dEaD", "0xabcdef", "5"}};
    }

    void official_vectors()
    {
        expect_equal("test Safe", detail::derive_safe_address(kSigner, kContracts), kSafe);
        expect_equal("production Safe of a real Polymarket account",
                     detail::derive_safe_address("0xAde4611dF7a34071A1886503f2Ab7D2bc1C68bC9", kContracts),
                     "0xa8Beb4744dc06f51355b92baa9a57dd7F127D006");

        OrderSigner signer(kKey);
        const detail::SafeCall single{{kContracts.collateral_adapter, kRedeemData, "0"}, detail::kSafeOperationCall};
        const auto digest = detail::safe_transaction_digest(kSafe, 137, single, "12");
        expect_equal("single-call SafeTx digest", to_hex(digest),
                     "0x1c9a8453e6198cefee86e8927b6043a5801560a1a52bd4bfc30fc6d7a845b36e");
        const auto signature = detail::sign_safe_digest(signer, digest);
        expect_equal("single-call Safe signature", signature, kSingleSignature);

        const auto payload = detail::build_safe_payload(kSigner, kSafe, single, "12", signature,
                                                        "Redeem positions for condition 0x6b04");
        const auto expected_payload = nlohmann::json::parse(
            R"({"type":"SAFE","from":"0x9d8A62f656a8d1615C1294fd71e9CFb3E4855A4F","to":"0xAdA100Db00Ca00073811820692005400218FcE1f",)"
            R"("proxyWallet":"0xe909E402b7FA6E29D2f91b343B44Ed29C1d498Ed","data":")" +
            kRedeemData + R"(","nonce":"12","signature":")" + kSingleSignature +
            R"(","metadata":"Redeem positions for condition 0x6b04","signatureParams":{"baseGas":"0","gasPrice":"0",)"
            R"("gasToken":"0x0000000000000000000000000000000000000000","operation":"0",)"
            R"("refundReceiver":"0x0000000000000000000000000000000000000000","safeTxnGas":"0"}})");
        check(payload == expected_payload, "submit payload mismatch:\n  " + payload.dump() + "\n  " + expected_payload.dump());

        const auto batch = detail::resolve_safe_call(multisend_calls(), kContracts);
        check(batch.operation == detail::kSafeOperationDelegateCall && batch.call.to == kContracts.safe_multisend,
              "batch is a DELEGATECALL into MultiSend");
        expect_equal("MultiSend packing", batch.call.data,
                     "0x8d80ff0a00000000000000000000000000000000000000000000000000000000000000200000000000000000000000"
                     "00000000000000000000000000000000000000019100ada100db00ca00073811820692005400218fce1f000000000000"
                     "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
                     "000000000000000000e401b7037c000000000000000000000000c011a7e12a19f7b1f670d46f03b03f3342e82dfb0000"
                     "0000000000000000000000000000000000000000000000000000000000006b049bc3befa02c22d0cfaee77625fc94e9a"
                     "6d571f00c015ba7a3b442e306fbc00000000000000000000000000000000000000000000000000000000000000800000"
                     "000000000000000000000000000000000000000000000000000000000002000000000000000000000000000000000000"
                     "000000000000000000000000000100000000000000000000000000000000000000000000000000000000000000020000"
                     "0000000000000000000000000000000000dead0000000000000000000000000000000000000000000000000000000000"
                     "0000050000000000000000000000000000000000000000000000000000000000000003abcdef00000000000000000000"
                     "0000000000");
        const auto batch_digest = detail::safe_transaction_digest(kSafe, 137, batch, "0");
        expect_equal("MultiSend SafeTx digest", to_hex(batch_digest),
                     "0x14e1b5b6845ccb56b46a0a288d3af9085534f67bb84afbe3a69ab532b42a04c3");
        expect_equal("MultiSend Safe signature", detail::sign_safe_digest(signer, batch_digest),
                     "0x706e5fa20a46a6af76dd3ed7e20f1a248b9d9469958bb55e36105b5fbf04ad8609deed4cb77bf83118980d8f5266f3"
                     "ec1be740a9838f5b32c5cf759e447d8ab21f");
        check(!detail::build_safe_payload(kSigner, kSafe, batch, "0", "0x", "").contains("value"),
              "zero value is omitted from the payload");
        check(detail::build_safe_payload(kSigner, kSafe, {{kSafe, "0x", "7"}, 0}, "0", "0x", "").at("value") == "7",
              "non-zero value is sent");
    }

    void retry_rules()
    {
        const auto retryable = [](long status, const std::string &body)
        { return detail::is_retryable_submit_error(detail::RelayerRequestError("/submit", status, body)); };
        check(retryable(429, ""), "rate limit is retryable");
        check(retryable(400, R"({"error":"Wallet busy: has active action"})"), "busy wallet is retryable");
        check(retryable(400, "wallet has in-flight action"), "in-flight action is retryable");
        check(retryable(400, "batch nonce 4 does not match on-chain nonce 12"), "stale nonce is retryable");
        check(!retryable(400, "batch nonce 12 does not match on-chain nonce 4"), "future nonce is not retryable");
        check(!retryable(400, "invalid signature"), "bad signature is not retryable");
        check(!retryable(401, "wallet busy: active action"), "auth failure is not retryable");
        check(!retryable(500, ""), "server error is not retryable");
    }

    // ---- client flows against fake relayer, RPC node and Gamma ----

    std::string rpc_reply(const clob_test::Request &request, const nlohmann::json &result)
    {
        const auto id = nlohmann::json::parse(request.body).at("id");
        return nlohmann::json{{"jsonrpc", "2.0"}, {"id", id}, {"result", result}}.dump();
    }

    void expect_rpc(clob_test::LocalServer &node, const std::string &method,
                    std::function<nlohmann::json(const nlohmann::json &)> result)
    {
        node.enqueue([method, result](const clob_test::Request &request)
                     {
            const auto body = nlohmann::json::parse(request.body);
            check(body.at("method") == method, "expected " + method + ", got " + body.at("method").dump());
            return rpc_reply(request, result(body.at("params"))); });
    }

    void expect_rpc_value(clob_test::LocalServer &node, const std::string &method, nlohmann::json result)
    {
        expect_rpc(node, method, [result](const nlohmann::json &) { return result; });
    }

    std::string balances_result(const std::string &yes, const std::string &no)
    {
        return to_hex(evm_abi_encode({EvmAbiValue::array({EvmAbiValue::uint256(yes), EvmAbiValue::uint256(no)})}));
    }

    nlohmann::json gamma_market(const std::string &condition)
    {
        return nlohmann::json::array({{{"id", "1"},
                                       {"conditionId", condition},
                                       {"version", "v1"},
                                       {"negRisk", false},
                                       {"clobTokenIds", nlohmann::json::array({"11", "12"})}}});
    }

    struct Harness
    {
        clob_test::LocalServer node;
        clob_test::LocalServer gamma;
        clob_test::LocalServer relayer;
        PositionClient client;

        static PositionClientConfig config(const std::string &node_url, const std::string &gamma_url,
                                           const std::string &relayer_url)
        {
            PositionClientConfig config;
            config.private_key = kKey;
            config.rpc_url = node_url;
            config.gamma_api_url = gamma_url;
            config.wallet_type = SignatureType::POLY_GNOSIS_SAFE;
            config.relayer_url = relayer_url;
            config.relayer_api_key = "relayer-key";
            config.relayer_retry_delay_ms = 1;
            return config;
        }

        Harness() : client(config(node.url(), gamma.url(), relayer.url())) {}

        void params(const std::string &nonce)
        {
            relayer.enqueue([nonce](const clob_test::Request &request)
                            {
                check(request.method == "GET" &&
                          request.target == "/v1/account/transactions/params?address=" + kSigner + "&type=SAFE",
                      "params request " + request.method + " " + request.target);
                check(request.headers.count("relayer_api_key") && request.headers.at("relayer_api_key") == "relayer-key" &&
                          request.headers.count("relayer_api_key_address") &&
                          request.headers.at("relayer_api_key_address") == kSigner,
                      "relayer auth headers");
                return nlohmann::json{{"address", kSafe}, {"nonce", nonce}}.dump(); });
        }

        size_t relayer_count(const std::string &target_prefix) const
        {
            size_t count = 0;
            for (const auto &request : relayer.requests())
                count += request.target.starts_with(target_prefix);
            return count;
        }
    };

    const auto kFast = std::chrono::milliseconds(5);
    const std::string kMinedHash = "0x" + std::string(64, 'e');

    nlohmann::json relayer_tx(const std::string &state, const std::string &hash)
    {
        return {{"transaction_id", "tx-1"}, {"transaction_hash", hash}, {"state", state}, {"error_msg", nullptr}};
    }

    void redeem_flow()
    {
        Harness h;
        check(h.client.wallet_address() == kSafe, "Safe wallet address");

        h.gamma.enqueue(gamma_market(kCondition).dump());
        expect_rpc(h.node, "eth_call", [](const nlohmann::json &params)
                   {
            const auto data = params.at(0).at("data").get<std::string>();
            check(data.find("e909e402b7fa6e29d2f91b343b44ed29c1d498ed") != std::string::npos,
                  "balances are read for the Safe");
            return nlohmann::json(balances_result("61333332", "0")); });
        expect_rpc_value(h.node, "eth_chainId", "0x89");
        h.params("12");
        h.relayer.enqueue([](const clob_test::Request &request)
                          {
            check(request.method == "POST" && request.target == "/submit", "submit request");
            const auto body = nlohmann::json::parse(request.body);
            check(body.at("signature") == kSingleSignature && body.at("data") == kRedeemData &&
                      body.at("to") == kContracts.collateral_adapter && body.at("proxyWallet") == kSafe &&
                      body.at("metadata") == "Redeem positions for condition " + kCondition,
                  "submit body " + body.dump());
            return nlohmann::json{{"transactionID", "tx-1"}, {"transactionHash", ""}, {"state", "STATE_NEW"}}.dump(); });

        const auto handle = h.client.redeem_positions(kCondition);
        check(handle.transaction_id() == "tx-1" && handle.transaction_hash().empty() &&
                  handle.transaction_hashes().empty(),
              "relayer handle before mining");

        h.relayer.enqueue(relayer_tx("STATE_NEW", "").dump());
        h.relayer.enqueue(relayer_tx("STATE_MINED", kMinedHash).dump());
        h.relayer.enqueue(relayer_tx("STATE_CONFIRMED", kMinedHash).dump());
        expect_rpc(h.node, "eth_getTransactionReceipt", [](const nlohmann::json &params)
                   {
            check(params.at(0) == kMinedHash, "receipt for the relayed hash");
            return nlohmann::json{{"transactionHash", kMinedHash}, {"blockHash", "0x" + std::string(64, 'b')},
                                  {"blockNumber", "0x1"}, {"gasUsed", "0x1"}, {"status", "0x1"},
                                  {"logs", nlohmann::json::array()}}; });
        const auto outcome = handle.wait(std::chrono::seconds(5), kFast);
        check(outcome.transaction_id == "tx-1" && outcome.transaction_hash == kMinedHash && outcome.receipt.success,
              "relayer outcome");
        check(h.relayer_count("/v1/account/transactions/tx-1") == 3, "polled until confirmed");

        h.relayer.enqueue(nlohmann::json{{"transaction_id", "tx-1"}, {"transaction_hash", ""}, {"state", "STATE_FAILED"},
                                         {"error_msg", "execution reverted"}}
                              .dump());
        expect_throws<TransactionFailedError>("relayer failure", [&]
                                              { (void)handle.wait(std::chrono::seconds(5), kFast); });
        for (int i = 0; i < 4; ++i)
            h.relayer.enqueue(relayer_tx("STATE_NEW", "").dump());
        expect_throws<TransactionTimeoutError>("relayer timeout", [&]
                                               { (void)handle.wait(std::chrono::milliseconds(12), kFast); });
    }

    void submit_retries()
    {
        Harness h;
        expect_rpc_value(h.node, "eth_chainId", "0x89");
        h.params("3");
        h.relayer.enqueue(R"({"error":"wallet busy: has active action"})", 400);
        h.params("4");
        h.relayer.enqueue([](const clob_test::Request &request)
                          {
            check(nlohmann::json::parse(request.body).at("nonce") == "4", "retry re-signs with the fresh nonce");
            return nlohmann::json{{"transactionID", "tx-2"}, {"transactionHash", kMinedHash}, {"state", "STATE_NEW"}}.dump(); });
        const auto handle = h.client.execute_calls({{kContracts.collateral_adapter, kRedeemData, "0"}});
        check(handle.transaction_id() == "tx-2" && handle.transaction_hash() == kMinedHash, "handle after retry");
        check(h.relayer_count("/submit") == 2, "one retry");

        h.params("5");
        h.relayer.enqueue(R"({"error":"invalid api key"})", 401);
        try
        {
            (void)h.client.execute_calls({{kContracts.collateral_adapter, kRedeemData, "0"}});
            check(false, "401 must throw");
        }
        catch (const detail::RelayerRequestError &error)
        {
            check(error.status() == 401, "401 surfaces as RelayerRequestError");
        }
        check(h.relayer_count("/submit") == 3, "401 is not retried");

        expect_throws<std::invalid_argument>("metadata over 500 characters", [&]
                                             { (void)h.client.execute_calls({{kSafe, "0x", "0"}}, std::string(501, 'm')); });
    }

    void batch_merge_is_one_multisend()
    {
        Harness h;
        h.gamma.enqueue(gamma_market(kCondition).dump());
        expect_rpc_value(h.node, "eth_call", balances_result("10", "20"));
        h.gamma.enqueue(gamma_market(kCondition2).dump());
        expect_rpc_value(h.node, "eth_call", balances_result("4", "4"));
        expect_rpc_value(h.node, "eth_chainId", "0x89");
        h.params("0");
        const auto expected = detail::resolve_safe_call(
            {detail::ctf_merge_positions_call(kContracts.collateral_adapter, kContracts.collateral_token, kCondition, "10"),
             detail::ctf_merge_positions_call(kContracts.collateral_adapter, kContracts.collateral_token, kCondition2, "4")},
            kContracts);
        h.relayer.enqueue([expected](const clob_test::Request &request)
                          {
            const auto body = nlohmann::json::parse(request.body);
            check(body.at("to") == kContracts.safe_multisend && body.at("data") == expected.call.data &&
                      body.at("signatureParams").at("operation") == "1" && body.at("metadata") == "Merge 2 positions",
                  "batch submit " + body.dump());
            return nlohmann::json{{"transactionID", "tx-3"}, {"state", "STATE_NEW"}}.dump(); });
        const auto handle = h.client.merge_multiple_positions({{kCondition, "max"}, {kCondition2, "max"}});
        check(handle.transaction_id() == "tx-3" && h.relayer_count("/submit") == 1, "one relayer submission");
    }

    void config_validation()
    {
        const auto base = []
        {
            PositionClientConfig config;
            config.private_key = kKey;
            config.rpc_url = "http://127.0.0.1:1";
            config.wallet_type = SignatureType::POLY_GNOSIS_SAFE;
            config.relayer_api_key = "k";
            return config;
        };
        {
            auto config = base();
            config.funder_address = "0xE909E402B7FA6E29D2F91B343B44ED29C1D498ED";
            PositionClient client(config);
            check(client.wallet_address() == kSafe, "matching funder in another case is accepted");
        }
        expect_throws<std::invalid_argument>("funder is not this key's Safe", [&]
                                             {
            auto config = base();
            config.funder_address = "0xa8Beb4744dc06f51355b92baa9a57dd7F127D006";
            PositionClient client(config); });
        expect_throws<std::invalid_argument>("missing relayer key", [&]
                                             {
            auto config = base();
            config.relayer_api_key.clear();
            PositionClient client(config); });
        expect_throws<std::invalid_argument>("proxy wallets are not supported yet", [&]
                                             {
            auto config = base();
            config.wallet_type = SignatureType::POLY_PROXY;
            PositionClient client(config); });
        expect_throws<std::invalid_argument>("EOA funder must be the signer", [&]
                                             {
            auto config = base();
            config.wallet_type = SignatureType::EOA;
            config.funder_address = kSafe;
            PositionClient client(config); });
    }
} // namespace

int main()
{
    official_vectors();
    retry_rules();
    redeem_flow();
    submit_retries();
    batch_merge_is_one_multisend();
    config_validation();
    if (failures != 0)
    {
        std::cerr << failures << " test_safe_relayer check(s) failed\n";
        return 1;
    }
    std::cout << "test_safe_relayer passed\n";
    return 0;
}
