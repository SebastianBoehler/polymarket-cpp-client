#pragma once

#include "websocket_client.hpp"
#include "websocket_test_server.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>

namespace websocket_test
{
    enum class TransportCallback
    {
        Connect,
        Disconnect,
        Error,
        Gap
    };

    inline const char *callback_name(TransportCallback callback)
    {
        switch (callback)
        {
        case TransportCallback::Connect:
            return "connect";
        case TransportCallback::Disconnect:
            return "disconnect";
        case TransportCallback::Error:
            return "error";
        case TransportCallback::Gap:
            return "gap";
        }
        return "unknown";
    }

    inline bool wait_for_disconnected(polymarket::WebSocketClient &client)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (client.state() == polymarket::WsState::DISCONNECTED)
            {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return client.state() == polymarket::WsState::DISCONNECTED;
    }

    inline bool run_transport_callback_lifecycle_test(TransportCallback callback,
                                                      bool call_stop)
    {
        auto initial_server = callback == TransportCallback::Error
                                  ? nullptr
                                  : std::make_unique<LocalWebSocketServer>();
        polymarket::WebSocketClient client;
        polymarket::WebSocketOptions options;
        options.reconnect_enabled = false;
        client.configure(options);
        client.set_url(initial_server ? initial_server->url() : "ws://127.0.0.1:0");

        std::promise<void> lifecycle_returned;
        auto returned = lifecycle_returned.get_future();
        std::atomic<bool> invoked{false};
        const auto invoke_lifecycle = [&]
        {
            if (invoked.exchange(true))
            {
                return;
            }
            if (call_stop)
            {
                client.stop();
            }
            else
            {
                client.disconnect();
            }
            lifecycle_returned.set_value();
        };

        switch (callback)
        {
        case TransportCallback::Connect:
            client.on_connect(invoke_lifecycle);
            break;
        case TransportCallback::Disconnect:
            client.on_disconnect(invoke_lifecycle);
            break;
        case TransportCallback::Error:
            client.on_error([&](const std::string &) { invoke_lifecycle(); });
            break;
        case TransportCallback::Gap:
            client.on_stream_gap([&](uint64_t) { invoke_lifecycle(); });
            break;
        }

        client.connect();
        if (callback == TransportCallback::Disconnect)
        {
            if (!initial_server->wait_for_connections(1, std::chrono::seconds(1)))
            {
                client.stop();
                std::cerr << "failed: disconnect callback setup\n";
                return false;
            }
            initial_server->close_clients();
        }

        const bool callback_returned =
            returned.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
        const bool disconnected = callback_returned && wait_for_disconnected(client);

        client.on_connect({});
        client.on_disconnect({});
        client.on_error({});
        client.on_stream_gap({});
        LocalWebSocketServer recovery_server;
        client.set_url(recovery_server.url());
        const bool restarted = disconnected && client.connect() &&
                               recovery_server.wait_for_connections(1, std::chrono::seconds(1));
        client.stop();

        if (!callback_returned || !disconnected || !restarted)
        {
            std::cerr << "failed: " << callback_name(callback) << " callback "
                      << (call_stop ? "stop" : "disconnect")
                      << " must return, finish teardown, and restart\n";
            return false;
        }
        return true;
    }

    inline bool run_transport_callback_lifecycle_tests()
    {
        for (const auto callback : {TransportCallback::Connect,
                                    TransportCallback::Disconnect,
                                    TransportCallback::Error,
                                    TransportCallback::Gap})
        {
            if (!run_transport_callback_lifecycle_test(callback, true) ||
                !run_transport_callback_lifecycle_test(callback, false))
            {
                return false;
            }
        }
        return true;
    }

    inline bool run_overlapping_callback_stop_test()
    {
        LocalWebSocketServer server;
        polymarket::WebSocketClient client;
        polymarket::WebSocketOptions options;
        options.reconnect_enabled = false;
        client.configure(options);
        client.set_url(server.url());

        std::mutex mutex;
        std::condition_variable cv;
        bool message_entered = false;
        bool release_message = false;
        std::promise<void> message_stopped;
        std::promise<void> disconnect_stopped;
        auto message_result = message_stopped.get_future();
        auto disconnect_result = disconnect_stopped.get_future();
        std::atomic<bool> disconnect_invoked{false};

        client.on_message([&](const std::string &)
                          {
            {
                std::unique_lock lock(mutex);
                message_entered = true;
                cv.notify_all();
                cv.wait(lock, [&] { return release_message; });
            }
            client.stop();
            message_stopped.set_value(); });
        client.on_disconnect([&]
                             {
            if (!disconnect_invoked.exchange(true))
            {
                client.stop();
                disconnect_stopped.set_value();
            } });

        bool ok = client.connect() &&
                  server.wait_for_connections(1, std::chrono::seconds(1)) &&
                  server.send_to_clients("pause-message-callback");
        {
            std::unique_lock lock(mutex);
            ok = cv.wait_for(lock, std::chrono::seconds(1),
                             [&] { return message_entered; }) &&
                 ok;
        }
        server.close_clients();
        ok = disconnect_result.wait_for(std::chrono::seconds(1)) ==
                 std::future_status::ready &&
             ok;
        {
            std::lock_guard lock(mutex);
            release_message = true;
            cv.notify_all();
        }
        ok = message_result.wait_for(std::chrono::seconds(1)) ==
                 std::future_status::ready &&
             ok;
        ok = wait_for_disconnected(client) && ok;
        client.stop();
        if (!ok)
        {
            std::cerr << "failed: overlapping transport/message callback stops deadlocked\n";
        }
        return ok;
    }
}
