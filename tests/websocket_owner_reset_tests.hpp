#pragma once

#include "websocket_client.hpp"
#include "websocket_test_server.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

namespace websocket_test
{
    enum class OwnerResetCallback
    {
        Message,
        SequencedMessage,
        TypedMessage,
        Connect,
        Disconnect,
        Error,
        Gap
    };

    inline const char *owner_reset_callback_name(OwnerResetCallback callback)
    {
        switch (callback)
        {
        case OwnerResetCallback::Message: return "message";
        case OwnerResetCallback::SequencedMessage: return "sequenced message";
        case OwnerResetCallback::TypedMessage: return "typed message";
        case OwnerResetCallback::Connect: return "connect";
        case OwnerResetCallback::Disconnect: return "disconnect";
        case OwnerResetCallback::Error: return "error";
        case OwnerResetCallback::Gap: return "gap";
        }
        return "unknown";
    }

    inline bool wait_for_expired(const std::weak_ptr<int> &lifetime,
                                 std::chrono::milliseconds timeout)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (lifetime.expired()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return lifetime.expired();
    }

    inline bool is_message_callback(OwnerResetCallback callback)
    {
        return callback == OwnerResetCallback::Message ||
               callback == OwnerResetCallback::SequencedMessage ||
               callback == OwnerResetCallback::TypedMessage;
    }

    inline bool run_owner_reset_test(OwnerResetCallback callback)
    {
        auto server = callback == OwnerResetCallback::Error
                          ? nullptr
                          : std::make_unique<LocalWebSocketServer>();
        auto owner = std::make_unique<polymarket::WebSocketClient>();
        polymarket::WebSocketOptions options;
        options.reconnect_enabled = false;
        owner->configure(options);
        owner->set_url(server ? server->url() : "ws://127.0.0.1:0");

        std::promise<void> reset_returned;
        auto returned = reset_returned.get_future();
        std::atomic<bool> invoked{false};
        std::atomic<bool> callback_after_reset{false};
        auto retained = std::make_shared<int>(1);
        std::weak_ptr<int> lifetime = retained;
        std::function<void()> reset_owner = [&, hold = retained]
        {
            (void)hold;
            if (invoked.exchange(true)) return;
            owner.reset();
            reset_returned.set_value();
        };

        switch (callback)
        {
        case OwnerResetCallback::Message:
            owner->on_message([reset_owner](const std::string &) { reset_owner(); });
            owner->on_sequenced_message(
                [&](const std::string &, uint64_t) { callback_after_reset.store(true); });
            break;
        case OwnerResetCallback::SequencedMessage:
            owner->on_sequenced_message(
                [reset_owner](const std::string &, uint64_t) { reset_owner(); });
            owner->on_typed_message(
                [&](const polymarket::TypedWebSocketMessage &)
                { callback_after_reset.store(true); });
            break;
        case OwnerResetCallback::TypedMessage:
            owner->on_typed_message(
                [reset_owner](const polymarket::TypedWebSocketMessage &) { reset_owner(); });
            break;
        case OwnerResetCallback::Connect:
            owner->on_connect(reset_owner);
            break;
        case OwnerResetCallback::Disconnect:
            owner->on_disconnect(reset_owner);
            break;
        case OwnerResetCallback::Error:
            owner->on_error([reset_owner](const std::string &) { reset_owner(); });
            break;
        case OwnerResetCallback::Gap:
            owner->on_stream_gap([reset_owner](uint64_t) { reset_owner(); });
            owner->on_connect([&] { callback_after_reset.store(true); });
            break;
        }
        reset_owner = {};
        retained.reset();

        auto *facade = owner.get();
        const bool started = facade->connect();
        bool triggered = true;
        if (is_message_callback(callback))
        {
            triggered = server->wait_for_connections(1, std::chrono::seconds(1)) &&
                        server->send_to_clients(R"({"event_type":"book"})");
        }
        else if (callback == OwnerResetCallback::Disconnect)
        {
            triggered = server->wait_for_connections(1, std::chrono::seconds(1));
            if (triggered) server->close_clients();
        }

        const bool callback_returned = triggered &&
            returned.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
        const bool state_destroyed = callback_returned &&
            wait_for_expired(lifetime, std::chrono::seconds(2));
        const bool transport_closed = !server || !callback_returned ||
            server->wait_for_no_clients(std::chrono::seconds(2));
        if (owner) owner.reset();

        if (!started || !triggered || !callback_returned || !state_destroyed ||
            !transport_closed || callback_after_reset.load())
        {
            std::cerr << "failed: " << owner_reset_callback_name(callback)
                      << " callback must safely destroy its WebSocket owner\n";
            return false;
        }
        return true;
    }

    inline bool run_owner_reset_tests()
    {
        for (const auto callback : {OwnerResetCallback::Message,
                                    OwnerResetCallback::SequencedMessage,
                                    OwnerResetCallback::TypedMessage,
                                    OwnerResetCallback::Connect,
                                    OwnerResetCallback::Disconnect,
                                    OwnerResetCallback::Error,
                                    OwnerResetCallback::Gap})
        {
            if (!run_owner_reset_test(callback)) return false;
        }
        return true;
    }
}
