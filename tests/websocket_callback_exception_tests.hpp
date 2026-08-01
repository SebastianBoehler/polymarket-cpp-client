#pragma once

#include "websocket_client.hpp"
#include "websocket_test_server.hpp"

#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

namespace websocket_test
{
    enum class ThrowingCallback
    {
        Message,
        SequencedMessage,
        TypedMessage,
        Connect,
        Disconnect,
        Error,
        Gap
    };

    inline const char *throwing_callback_name(ThrowingCallback callback)
    {
        switch (callback)
        {
        case ThrowingCallback::Message: return "message";
        case ThrowingCallback::SequencedMessage: return "sequenced message";
        case ThrowingCallback::TypedMessage: return "typed message";
        case ThrowingCallback::Connect: return "connect";
        case ThrowingCallback::Disconnect: return "disconnect";
        case ThrowingCallback::Error: return "error";
        case ThrowingCallback::Gap: return "stream gap";
        }
        return "unknown";
    }

    inline bool wait_for_callback_error(polymarket::WebSocketClient &client)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (client.stats().callback_errors > 0) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return client.stats().callback_errors > 0;
    }

    inline bool is_throwing_message_callback(ThrowingCallback callback)
    {
        return callback == ThrowingCallback::Message ||
               callback == ThrowingCallback::SequencedMessage ||
               callback == ThrowingCallback::TypedMessage;
    }

    inline bool run_callback_exception_test(ThrowingCallback callback)
    {
        auto server = callback == ThrowingCallback::Error
                          ? nullptr
                          : std::make_unique<LocalWebSocketServer>();
        polymarket::WebSocketClient client;
        polymarket::WebSocketOptions options;
        options.reconnect_enabled = false;
        client.configure(options);
        client.set_url(server ? server->url() : "ws://127.0.0.1:0");

        std::promise<void> callback_entered;
        auto entered = callback_entered.get_future();
        std::atomic<bool> entered_once{false};
        const auto throw_error = [&]
        {
            if (!entered_once.exchange(true)) callback_entered.set_value();
            throw std::runtime_error("boom");
        };

        std::promise<std::string> error_reported;
        auto reported = error_reported.get_future();
        std::atomic<bool> reported_once{false};
        if (callback == ThrowingCallback::Error)
            client.on_error([&](const std::string &) { throw_error(); });
        else
            client.on_error([&](const std::string &message)
                            {
                if (!reported_once.exchange(true)) error_reported.set_value(message); });

        switch (callback)
        {
        case ThrowingCallback::Message:
            client.on_message([&](const std::string &) { throw_error(); });
            break;
        case ThrowingCallback::SequencedMessage:
            client.on_sequenced_message(
                [&](const std::string &, uint64_t) { throw_error(); });
            break;
        case ThrowingCallback::TypedMessage:
            client.on_typed_message(
                [&](const polymarket::TypedWebSocketMessage &) { throw_error(); });
            break;
        case ThrowingCallback::Connect:
            client.on_connect(throw_error);
            break;
        case ThrowingCallback::Disconnect:
            client.on_disconnect(throw_error);
            break;
        case ThrowingCallback::Gap:
            client.on_stream_gap([&](uint64_t) { throw_error(); });
            break;
        case ThrowingCallback::Error:
            break;
        }

        const bool started = client.connect();
        bool triggered = true;
        if (is_throwing_message_callback(callback))
        {
            triggered = server->wait_for_connections(1, std::chrono::seconds(1)) &&
                        server->send_to_clients(R"({"event_type":"book"})");
        }
        else if (callback == ThrowingCallback::Disconnect)
        {
            triggered = server->wait_for_connections(1, std::chrono::seconds(1));
            if (triggered) server->close_clients();
        }

        const bool invoked = triggered &&
            entered.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
        bool failure_reported = true;
        if (callback != ThrowingCallback::Error)
        {
            failure_reported = reported.wait_for(std::chrono::seconds(2)) ==
                                   std::future_status::ready &&
                               reported.get().find(throwing_callback_name(callback)) !=
                                   std::string::npos;
        }
        const bool counted = invoked && wait_for_callback_error(client);
        const bool typed_not_misclassified = callback != ThrowingCallback::TypedMessage ||
                                             client.stats().parse_errors == 0;
        client.stop();

        if (!started || !triggered || !invoked || !failure_reported || !counted ||
            !typed_not_misclassified)
        {
            std::cerr << "failed: " << throwing_callback_name(callback)
                      << " callback exception must be contained and reported\n";
            return false;
        }
        return true;
    }

    inline bool run_callback_exception_tests()
    {
        for (const auto callback : {ThrowingCallback::Message,
                                    ThrowingCallback::SequencedMessage,
                                    ThrowingCallback::TypedMessage,
                                    ThrowingCallback::Connect,
                                    ThrowingCallback::Disconnect,
                                    ThrowingCallback::Error,
                                    ThrowingCallback::Gap})
        {
            if (!run_callback_exception_test(callback)) return false;
        }
        return true;
    }
}
