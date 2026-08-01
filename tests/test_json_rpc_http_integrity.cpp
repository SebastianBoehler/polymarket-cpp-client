#include "evm_event_indexer.hpp"
#include "json_rpc_client.hpp"
#include "../src/clob_client_test_fixture.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace polymarket;

namespace
{
    using Factory = std::function<std::string(const clob_test::Request &)>;

    uint64_t request_id(const clob_test::Request &request)
    {
        return nlohmann::json::parse(request.body).at("id").get<uint64_t>();
    }

    nlohmann::json response_base(const clob_test::Request &request)
    {
        return {{"jsonrpc", "2.0"}, {"id", request_id(request)}};
    }

    std::string call_error(Factory factory)
    {
        clob_test::LocalServer server;
        server.enqueue(std::move(factory));
        EvmJsonRpcHttpClient client(server.url());
        try
        {
            (void)client.call("eth_chainId", nlohmann::json::array());
        }
        catch (const std::exception &error)
        {
            return error.what();
        }
        return {};
    }

    bool rejects_invalid(Factory factory, const std::string &label)
    {
        const auto error = call_error(std::move(factory));
        return clob_test::check(
            error.starts_with("invalid JSON-RPC response:"),
            label + " must be rejected as an invalid JSON-RPC response");
    }

    bool test_response_envelope_contract()
    {
        bool ok = true;
        {
            clob_test::LocalServer server;
            server.enqueue([](const clob_test::Request &request)
                           {
                auto body = response_base(request);
                body["result"] = nullptr;
                return body.dump(); });
            EvmJsonRpcHttpClient client(server.url());
            ok &= clob_test::check(
                client.call("eth_chainId", nlohmann::json::array()).is_null(),
                "a present null result must remain a valid JSON-RPC result");
        }

        const std::vector<std::pair<std::string, Factory>> invalid = {
            {"non-object body", [](const auto &) { return "[]"; }},
            {"missing version", [](const auto &request)
             { return nlohmann::json{{"id", request_id(request)}, {"result", true}}.dump(); }},
            {"wrong version", [](const auto &request)
             { return nlohmann::json{{"jsonrpc", "1.0"}, {"id", request_id(request)}, {"result", true}}.dump(); }},
            {"missing id", [](const auto &)
             { return nlohmann::json{{"jsonrpc", "2.0"}, {"result", true}}.dump(); }},
            {"string id", [](const auto &request)
             {
                 return nlohmann::json{{"jsonrpc", "2.0"},
                                       {"id", std::to_string(request_id(request))},
                                       {"result", true}}
                     .dump(); }},
            {"mismatched id", [](const auto &request)
             { return nlohmann::json{{"jsonrpc", "2.0"}, {"id", request_id(request) + 1}, {"result", true}}.dump(); }},
            {"missing result and error", [](const auto &request)
             { return response_base(request).dump(); }},
            {"result and error", [](const auto &request)
             {
                auto body = response_base(request);
                body["result"] = true;
                body["error"] = {{"code", -32000}, {"message", "failure"}};
                return body.dump(); }},
            {"non-object error", [](const auto &request)
             {
                auto body = response_base(request);
                body["error"] = "failure";
                return body.dump(); }},
            {"error without integer code", [](const auto &request)
             {
                auto body = response_base(request);
                body["error"] = {{"code", 1.5}, {"message", "failure"}};
                return body.dump(); }},
            {"error without string message", [](const auto &request)
             {
                auto body = response_base(request);
                body["error"] = {{"code", -32000}, {"message", 7}};
                return body.dump(); }}};
        for (const auto &[label, factory] : invalid)
            ok &= rejects_invalid(factory, label);

        const auto error = call_error([](const auto &request)
                                      {
            auto body = response_base(request);
            body["error"] = {{"code", -32000}, {"message", "failure"}};
            return body.dump(); });
        ok &= clob_test::check(error.starts_with("JSON-RPC error:"),
                               "a structurally valid JSON-RPC error must be surfaced");
        return ok;
    }

    bool test_get_logs_requires_array()
    {
        clob_test::LocalServer server;
        server.enqueue([](const clob_test::Request &request)
                       {
            auto body = response_base(request);
            body["result"] = nlohmann::json::object();
            return body.dump(); });
        EvmJsonRpcHttpClient client(server.url());
        try
        {
            (void)client.get_logs({});
        }
        catch (const std::exception &)
        {
            return true;
        }
        return clob_test::check(false, "eth_getLogs must reject a non-array result");
    }

    class MemoryCursorStore final : public EvmBlockCursorStore
    {
    public:
        std::optional<uint64_t> load(const std::string &) override { return cursor; }
        void save(const std::string &, uint64_t block) override
        {
            cursor = block;
            ++save_count;
        }

        std::optional<uint64_t> cursor;
        std::size_t save_count{0};
    };

    bool indexer_rejects_without_advancing(Factory logs_response,
                                            const std::string &label)
    {
        clob_test::LocalServer server;
        server.enqueue([](const clob_test::Request &request)
                       {
            auto body = response_base(request);
            body["result"] = "0x1";
            return body.dump(); });
        server.enqueue(std::move(logs_response));

        auto store = std::make_shared<MemoryCursorStore>();
        EvmEventIndexerConfig config;
        config.rpc_http_url = server.url();
        config.rpc_ws_url = "ws://127.0.0.1:1";
        config.cursor_name = "http-integrity";
        config.start_block = 1;
        config.batch_size = 1;
        EvmEventIndexer indexer(config, store);

        bool threw = false;
        try
        {
            (void)indexer.catch_up();
        }
        catch (const std::exception &)
        {
            threw = true;
        }
        bool ok = clob_test::check(threw, label + " must abort indexer catch-up");
        ok &= clob_test::check(!store->cursor && store->save_count == 0,
                               label + " must not advance the indexer cursor");
        return ok;
    }

    bool test_indexer_cursor_integrity()
    {
        bool ok = indexer_rejects_without_advancing(
            [](const auto &request) { return response_base(request).dump(); },
            "a malformed HTTP 200 JSON-RPC envelope");
        ok &= indexer_rejects_without_advancing(
            [](const auto &request)
            {
                return nlohmann::json{{"jsonrpc", "2.0"},
                                      {"id", request_id(request) + 1},
                                      {"result", nlohmann::json::array()}}
                    .dump(); },
            "a mismatched HTTP 200 JSON-RPC response");
        return ok;
    }
}

int main()
{
    bool ok = test_response_envelope_contract();
    ok &= test_get_logs_requires_array();
    ok &= test_indexer_cursor_integrity();
    return ok ? 0 : 1;
}
