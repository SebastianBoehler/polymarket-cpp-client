#include "json_rpc_client.hpp"
#include "websocket_test_server.hpp"

#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

using namespace polymarket;
using namespace std::chrono_literals;

namespace
{
    struct Owner
    {
        std::unique_ptr<EvmJsonRpcWsClient> client;
    };

    struct Counts
    {
        std::atomic<unsigned int> callbacks{0};
        std::atomic<unsigned int> resets{0};
        std::atomic<unsigned int> errors{0};
    };

    constexpr const char *log_message =
        R"({"jsonrpc":"2.0","method":"eth_subscription","params":{"subscription":"0x1","result":{"address":"0x0000000000000000000000000000000000000001","blockHash":"0x01","blockNumber":"0x1","transactionHash":"0x02","transactionIndex":"0x0","logIndex":"0x0","data":"0x","topics":[]}}})";
    constexpr const char *pending_message =
        R"({"jsonrpc":"2.0","method":"eth_subscription","params":{"subscription":"0x2","result":"0xtransaction"}})";
    constexpr const char *head_message =
        R"({"jsonrpc":"2.0","method":"eth_subscription","params":{"subscription":"0x3","result":{"number":"0x2","hash":"0x03"}}})";
    constexpr const char *error_message =
        R"({"jsonrpc":"2.0","id":7,"error":{"code":-32000,"message":"fixture error"}})";

    template <typename Predicate>
    bool wait_until(Predicate predicate, std::chrono::milliseconds timeout = 1s)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (predicate()) return true;
            std::this_thread::sleep_for(2ms);
        }
        return predicate();
    }

    bool connect_and_send(EvmJsonRpcWsClient &client,
                          websocket_test::LocalWebSocketServer &server,
                          const std::string &message)
    {
        return client.connect() && server.wait_for_connections(1, 1s) &&
               server.send_to_clients(message);
    }

    enum class ResetCallback
    {
        Log,
        Pending,
        Head,
        Error
    };

    bool owner_reset(ResetCallback reset_at)
    {
        websocket_test::LocalWebSocketServer server;
        auto owner = std::make_shared<Owner>();
        auto counts = std::make_shared<Counts>();
        owner->client = std::make_unique<EvmJsonRpcWsClient>(server.url());
        auto *client = owner->client.get();
        const auto reset = [owner, counts]
        {
            counts->callbacks++;
            owner->client.reset();
            counts->resets++;
        };
        client->on_log([reset](const EvmLog &) { reset(); });
        client->on_pending_transaction([reset](const std::string &) { reset(); });
        client->on_head([reset](const nlohmann::json &) { reset(); });
        client->on_error([reset](const std::string &) { reset(); });

        const char *message = log_message;
        if (reset_at == ResetCallback::Pending) message = pending_message;
        else if (reset_at == ResetCallback::Head) message = head_message;
        else if (reset_at == ResetCallback::Error) message = error_message;
        if (!connect_and_send(*client, server, message) ||
            !wait_until([&] { return counts->resets.load() == 1; }))
            return false;
        std::this_thread::sleep_for(100ms);
        return counts->callbacks.load() == 1 && counts->resets.load() == 1;
    }

    bool exception_after_owner_reset()
    {
        websocket_test::LocalWebSocketServer server;
        auto owner = std::make_shared<Owner>();
        auto counts = std::make_shared<Counts>();
        owner->client = std::make_unique<EvmJsonRpcWsClient>(server.url());
        auto *client = owner->client.get();
        client->on_error(
            [counts](const std::string &) { counts->errors++; });
        client->on_log(
            [owner, counts](const EvmLog &)
            {
                owner->client.reset();
                counts->resets++;
                throw std::runtime_error("callback failed after reset");
            });
        if (!connect_and_send(*client, server, log_message) ||
            !wait_until([&] { return counts->resets.load() == 1; }))
            return false;
        std::this_thread::sleep_for(100ms);
        return counts->errors.load() == 0;
    }

    bool setter_during_dispatch()
    {
        websocket_test::LocalWebSocketServer server;
        EvmJsonRpcWsClient client(server.url());
        std::promise<void> entered;
        std::promise<void> resume;
        std::promise<void> completed;
        auto resume_future = resume.get_future().share();
        client.on_log(
            [&](const EvmLog &)
            {
                entered.set_value();
                resume_future.wait();
                completed.set_value();
            });
        if (!connect_and_send(client, server, log_message)) return false;
        auto entered_future = entered.get_future();
        if (entered_future.wait_for(1s) != std::future_status::ready)
            return false;
        std::thread setter([&]
                           { client.on_log([](const EvmLog &) {}); });
        setter.join();
        resume.set_value();
        auto completed_future = completed.get_future();
        const bool ok = completed_future.wait_for(1s) == std::future_status::ready;
        client.stop();
        return ok;
    }
}

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        std::cerr << "expected one scenario\n";
        return 2;
    }
    const std::string scenario = argv[1];
    bool ok = false;
    if (scenario == "log") ok = owner_reset(ResetCallback::Log);
    else if (scenario == "pending") ok = owner_reset(ResetCallback::Pending);
    else if (scenario == "head") ok = owner_reset(ResetCallback::Head);
    else if (scenario == "error") ok = owner_reset(ResetCallback::Error);
    else if (scenario == "reset-throw") ok = exception_after_owner_reset();
    else if (scenario == "setter-dispatch") ok = setter_during_dispatch();
    else
    {
        std::cerr << "unknown scenario: " << scenario << '\n';
        return 2;
    }
    if (!ok) std::cerr << "failed: " << scenario << '\n';
    return ok ? 0 : 1;
}
