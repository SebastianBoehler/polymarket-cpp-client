#include "json_rpc_client.hpp"
#include "../src/clob_client_test_fixture.hpp"

#include <functional>
#include <string>

using namespace polymarket;

namespace
{
    const std::string kHash = "0x" + std::string(64, 'a');
    const std::string kBlockHash = "0x" + std::string(64, 'b');
    const std::string kAddress = "0xAde4611dF7a34071A1886503f2Ab7D2bc1C68bC9";
    const std::string kAdapter = "0xAdA100Db00Ca00073811820692005400218FcE1f";

    // Serves one JSON-RPC result and exposes the parsed request for assertions.
    struct OneCall
    {
        clob_test::LocalServer server;
        EvmJsonRpcHttpClient client;

        explicit OneCall(nlohmann::json result) : client(server.url())
        {
            server.enqueue([result](const clob_test::Request &request)
                           {
                const auto id = nlohmann::json::parse(request.body).at("id");
                return nlohmann::json{{"jsonrpc", "2.0"}, {"id", id}, {"result", result}}.dump(); });
        }

        nlohmann::json request() const
        {
            const auto requests = server.requests();
            return requests.empty() ? nlohmann::json() : nlohmann::json::parse(requests.front().body);
        }
    };

    bool sent(const OneCall &rpc, const std::string &method, const nlohmann::json &params)
    {
        const auto request = rpc.request();
        return clob_test::check(request.value("method", "") == method && request.at("params") == params,
                                method + " sent unexpected request: " + request.dump());
    }

    bool rejects(nlohmann::json result, const std::function<void(EvmJsonRpcHttpClient &)> &action,
                 const std::string &label)
    {
        OneCall rpc(std::move(result));
        try
        {
            action(rpc.client);
        }
        catch (const std::runtime_error &error)
        {
            return clob_test::check(std::string(error.what()).starts_with("invalid JSON-RPC result:"),
                                    label + " rejected with unexpected message: " + error.what());
        }
        return clob_test::check(false, label + " was accepted");
    }

    nlohmann::json receipt_json(const std::string &status)
    {
        return {{"transactionHash", kHash},
                {"blockHash", kBlockHash},
                {"blockNumber", "0x5a8a1ee"},
                {"gasUsed", "0x1d4c0"},
                {"effectiveGasPrice", "0x7558bdb00"},
                {"status", status},
                {"logs", nlohmann::json::array({{{"address", kAdapter},
                                                 {"blockHash", kBlockHash},
                                                 {"blockNumber", "0x5a8a1ee"},
                                                 {"transactionHash", kHash},
                                                 {"transactionIndex", "0x3"},
                                                 {"logIndex", "0x7"},
                                                 {"data", "0x"},
                                                 {"topics", nlohmann::json::array({kHash})}}})}};
    }

    bool test_requests_and_results()
    {
        bool ok = true;
        {
            OneCall rpc("0x89");
            ok &= clob_test::check(rpc.client.chain_id() == 137, "chain_id must parse 0x89");
            ok &= sent(rpc, "eth_chainId", nlohmann::json::array());
        }
        {
            OneCall rpc("0x4d2");
            ok &= clob_test::check(rpc.client.get_transaction_count(kAddress) == "0x4d2", "nonce result");
            ok &= sent(rpc, "eth_getTransactionCount", {kAddress, "pending"});
        }
        {
            OneCall rpc("0x7558bdb00");
            ok &= clob_test::check(rpc.client.gas_price() == "0x7558bdb00", "gas price result");
            ok &= sent(rpc, "eth_gasPrice", nlohmann::json::array());
        }
        {
            OneCall rpc("0x3d090");
            EvmCallRequest request{kAdapter, "0x72ce4275", kAddress, ""};
            ok &= clob_test::check(rpc.client.estimate_gas(request) == "0x3d090", "estimate result");
            ok &= sent(rpc, "eth_estimateGas",
                       nlohmann::json::array({{{"to", kAdapter}, {"data", "0x72ce4275"}, {"from", kAddress}}}));
        }
        {
            OneCall rpc("0x" + std::string(64, '0'));
            EvmCallRequest request{kAdapter, "0x4e1273f4", "", "0x0"};
            ok &= clob_test::check(rpc.client.eth_call(request) == "0x" + std::string(64, '0'), "eth_call result");
            ok &= sent(rpc, "eth_call",
                       {{{"to", kAdapter}, {"data", "0x4e1273f4"}, {"value", "0x0"}}, "latest"});
        }
        {
            OneCall rpc(kHash);
            ok &= clob_test::check(rpc.client.send_raw_transaction("0xf86c") == kHash, "send raw result");
            ok &= sent(rpc, "eth_sendRawTransaction", {"0xf86c"});
        }
        {
            OneCall rpc(nullptr);
            ok &= clob_test::check(!rpc.client.get_transaction_receipt(kHash).has_value(),
                                   "pending receipt must be empty");
            ok &= sent(rpc, "eth_getTransactionReceipt", {kHash});
        }
        {
            OneCall rpc(receipt_json("0x1"));
            const auto receipt = rpc.client.get_transaction_receipt(kHash);
            ok &= clob_test::check(receipt && receipt->success && receipt->transaction_hash == kHash &&
                                       receipt->block_number == "0x5a8a1ee" && receipt->gas_used == "0x1d4c0" &&
                                       receipt->effective_gas_price == "0x7558bdb00" &&
                                       receipt->logs.size() == 1 && receipt->logs[0].log_index == "0x7",
                                   "successful receipt fields");
        }
        {
            auto body = receipt_json("0x0");
            body.erase("effectiveGasPrice");
            OneCall rpc(body);
            const auto receipt = rpc.client.get_transaction_receipt(kHash);
            ok &= clob_test::check(receipt && !receipt->success && receipt->effective_gas_price.empty(),
                                   "reverted receipt without effectiveGasPrice");
        }
        return ok;
    }

    bool test_malformed_results()
    {
        bool ok = true;
        const auto chain = [](EvmJsonRpcHttpClient &c)
        { (void)c.chain_id(); };
        const auto price = [](EvmJsonRpcHttpClient &c)
        { (void)c.gas_price(); };
        const auto send = [](EvmJsonRpcHttpClient &c)
        { (void)c.send_raw_transaction("0x00"); };
        const auto call = [](EvmJsonRpcHttpClient &c)
        { (void)c.eth_call({kAdapter, "0x", "", ""}); };
        const auto receipt = [](EvmJsonRpcHttpClient &c)
        { (void)c.get_transaction_receipt(kHash); };

        ok &= rejects(137, chain, "numeric chain id");
        ok &= rejects("0x1" + std::string(16, '0'), chain, "chain id over 64 bits");
        ok &= rejects("0x", price, "empty quantity");
        ok &= rejects("12", price, "unprefixed quantity");
        ok &= rejects("0xzz", price, "non-hex quantity");
        ok &= rejects("0x" + std::string(65, '1'), price, "quantity over uint256");
        ok &= rejects("0xabc", call, "odd-length call data");
        ok &= rejects(nullptr, call, "null call data");
        ok &= rejects("0x1234", send, "short transaction hash");
        ok &= rejects("not-an-object", receipt, "string receipt");

        for (const auto *field : {"transactionHash", "blockHash", "blockNumber", "gasUsed", "status", "logs"})
        {
            auto body = receipt_json("0x1");
            body.erase(field);
            ok &= rejects(body, receipt, std::string("receipt without ") + field);
        }
        ok &= rejects(receipt_json("0x2"), receipt, "receipt status 0x2");
        auto bad_logs = receipt_json("0x1");
        bad_logs["logs"] = "none";
        ok &= rejects(bad_logs, receipt, "receipt logs not an array");
        return ok;
    }
} // namespace

int main()
{
    bool ok = test_requests_and_results();
    ok &= test_malformed_results();
    if (!ok)
        return 1;
    std::cout << "test_json_rpc_transactions passed\n";
    return 0;
}
