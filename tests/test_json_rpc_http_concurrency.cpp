#include "json_rpc_client.hpp"
#include "../src/clob_client_test_fixture.hpp"

#include <barrier>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

using namespace polymarket;

int main()
{
    constexpr std::size_t call_count = 128;
    clob_test::LocalServer server;
    for (std::size_t i = 0; i < call_count; ++i)
        server.enqueue([](const clob_test::Request &request)
                       {
            const auto id = nlohmann::json::parse(request.body).at("id");
            return nlohmann::json{{"jsonrpc", "2.0"},
                                  {"id", id},
                                  {"result", true}}
                .dump(); });

    EvmJsonRpcHttpClient client(server.url());
    client.set_timeout_ms(10000);

    std::barrier start(static_cast<std::ptrdiff_t>(call_count + 1));
    std::mutex errors_mutex;
    std::vector<std::string> errors;
    std::vector<std::thread> workers;
    workers.reserve(call_count);
    for (std::size_t i = 0; i < call_count; ++i)
    {
        workers.emplace_back([&]
                             {
            start.arrive_and_wait();
            try
            {
                if (client.call("eth_chainId", nlohmann::json::array()) != true)
                    throw std::runtime_error("unexpected JSON-RPC result");
            }
            catch (const std::exception &error)
            {
                std::lock_guard<std::mutex> lock(errors_mutex);
                errors.push_back(error.what());
            } });
    }
    start.arrive_and_wait();
    for (auto &worker : workers)
        worker.join();

    bool ok = clob_test::check(errors.empty(), "concurrent JSON-RPC calls must succeed");
    const auto requests = server.requests();
    ok &= clob_test::check(requests.size() == call_count,
                           "fixture must receive every concurrent JSON-RPC call");

    std::set<uint64_t> ids;
    for (const auto &request : requests)
    {
        const auto body = nlohmann::json::parse(request.body);
        if (!body.contains("id") || !body["id"].is_number_unsigned())
        {
            ok = clob_test::check(false, "every JSON-RPC request must have an unsigned ID") && ok;
            continue;
        }
        ids.insert(body["id"].get<uint64_t>());
    }
    ok &= clob_test::check(ids.size() == call_count,
                           "concurrent JSON-RPC calls must allocate unique request IDs");
    if (!ids.empty())
    {
        ok &= clob_test::check(*ids.begin() == 1 && *ids.rbegin() == call_count,
                               "concurrent JSON-RPC request IDs must form one contiguous sequence");
    }
    return ok ? 0 : 1;
}
