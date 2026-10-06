#pragma once

#include "approval_calls.hpp"
#include "position_test_support.hpp"

#include <string>
#include <vector>

// Scripted RPC helpers shared by the trading approval flow tests.
namespace approvals_test
{
    using namespace position_test;

    const std::string recipient = "0x000000000000000000000000000000000000dEaD";

    // Built on use: a namespace-scope inline variable may be initialized
    // before kContracts, which Apple clang does.
    inline TradingApprovals required_set()
    {
        return required_trading_approvals(kContracts);
    }

    inline std::string word(const std::string &value)
    {
        return to_hex(evm_abi_encode({EvmAbiValue::uint256(value)}));
    }

    // Answers the 17 approval checks for `owner`. Results are given in
    // required order: ERC-20 allowances, then isApprovedForAll flags.
    inline void expect_approval_reads(clob_test::LocalServer &node, const std::string &owner,
                                      const std::vector<std::string> &results)
    {
        const auto checks = detail::trading_approval_check_calls(required_set(), owner);
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

    inline std::vector<std::string> all_approved()
    {
        const auto required = required_set();
        std::vector<std::string> results(required.erc20.size(), word(max_uint256));
        results.resize(required.erc20.size() + required.erc1155.size(), word("1"));
        return results;
    }

    inline void expect_transaction(clob_test::LocalServer &node, const ContractCall &call,
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

    inline void expect_mined(clob_test::LocalServer &node)
    {
        expect_rpc(node, "eth_getTransactionReceipt", [](const nlohmann::json &params)
                   { return receipt(params.at(0).get<std::string>(), true); });
    }

    inline PositionClientConfig base_config(const std::string &node_url)
    {
        PositionClientConfig config;
        config.private_key = kKey;
        config.rpc_url = node_url;
        config.rpc_timeout_ms = 5000;
        return config;
    }

    inline size_t count(const clob_test::LocalServer &node, const std::string &method)
    {
        size_t n = 0;
        for (const auto &request : node.requests())
            n += nlohmann::json::parse(request.body).at("method") == method;
        return n;
    }

    // JSON-RPC error reply, as a node answers eth_estimateGas for a reverting call.
    inline void expect_rpc_error(clob_test::LocalServer &node, const std::string &method)
    {
        node.enqueue(
            [method](const clob_test::Request &request)
            {
                const auto body = nlohmann::json::parse(request.body);
                check(body.at("method") == method, "expected " + method);
                return nlohmann::json{{"jsonrpc", "2.0"},
                                      {"id", body.at("id")},
                                      {"error", {{"code", 3}, {"message", "execution reverted"}}}}
                    .dump();
            });
    }

    // Two missing approvals: neg-risk exchange allowance, then CTF operator.
    inline void expect_two_missing(clob_test::LocalServer &node)
    {
        auto results = all_approved();
        results[1] = word("0");
        results[7] = word("0");
        expect_approval_reads(node, kWallet, results);
    }

} // namespace approvals_test
