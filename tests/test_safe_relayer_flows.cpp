// PositionClient Safe flows against fake relayer, RPC node and Gamma servers.
#include "safe_relayer.hpp"
#include "position_test_support.hpp"

using namespace position_test;

namespace
{
    const std::string kSigner = kWallet;

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
        expect_rpc(h.node, "eth_chainId", "0x89");
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
        expect_rpc(h.node, "eth_chainId", "0x89");
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
        expect_rpc(h.node, "eth_call", balances_result("10", "20"));
        h.gamma.enqueue(gamma_market(kCondition2).dump());
        expect_rpc(h.node, "eth_call", balances_result("4", "4"));
        expect_rpc(h.node, "eth_chainId", "0x89");
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
    redeem_flow();
    submit_retries();
    batch_merge_is_one_multisend();
    config_validation();
    return check_support::finish("test_safe_relayer_flows");
}
