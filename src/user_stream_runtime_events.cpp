#include "user_stream_runtime.hpp"

#include <exception>
#include <iostream>
#include <string>
#include <type_traits>
#include <variant>

namespace polymarket::detail
{
    namespace
    {
        // CLOB closes the user channel with policy violation on bad credentials.
        constexpr uint16_t policy_violation_close_code = 1008;
    }

    void UserStreamRuntime::handle_stream_gap(uint64_t websocket_generation)
    {
        if (!owner_active_.load(std::memory_order_acquire) ||
            !stream_active_.load(std::memory_order_acquire))
            return;
        stream_generation_.fetch_add(1);
        // Every gap closes or reopens the transport, so the matching connect
        // arrives on this websocket generation once subscriptions are replayed.
        recovery_generation_.store(websocket_generation, std::memory_order_release);
        const auto callbacks = callbacks_snapshot();
        if (callbacks->gap) callbacks->gap();
    }

    void UserStreamRuntime::handle_connect()
    {
        if (!owner_active_.load(std::memory_order_acquire) ||
            !stream_active_.load(std::memory_order_acquire))
            return;
        auto pending = recovery_generation_.load(std::memory_order_acquire);
        // A newer gap means this connection is already stale; its own
        // reconnect will report recovery instead.
        if (pending == 0 || pending != websocket_.stream_generation() ||
            !recovery_generation_.compare_exchange_strong(pending, 0))
            return;
        const auto callbacks = callbacks_snapshot();
        if (callbacks->recovered) callbacks->recovered();
    }

    void UserStreamRuntime::handle_close(uint16_t code, const std::string &reason)
    {
        if (code != policy_violation_close_code ||
            !owner_active_.load(std::memory_order_acquire) ||
            !stream_active_.load(std::memory_order_acquire))
            return;
        // Reconnecting with rejected credentials would loop forever. stop()
        // rather than disconnect() so a blocked or later run() returns too;
        // connect() clears the stop for a retry.
        authentication_failed_.store(true, std::memory_order_release);
        deactivate_stream();
        websocket_.stop();
        const auto error = "user stream rejected by server (" + std::to_string(code) +
                           "): " + reason;
        std::cerr << "[WS] " << error << '\n';
        const auto callbacks = callbacks_snapshot();
        if (callbacks->error) callbacks->error(error);
    }

    void UserStreamRuntime::handle_message(const std::string &message,
                                           uint64_t websocket_generation)
    {
        const auto generation = stream_generation_.load();
        if (!stream_is_current(generation, websocket_generation) ||
            message.empty() || message == "PONG")
            return;

        std::vector<UserStreamEvent> events;
        try
        {
            events = parse_user_events(message);
        }
        catch (const std::exception &error)
        {
            if (!owner_active_.load(std::memory_order_acquire)) return;
            std::cerr << "[WS] User stream parse error: " << error.what() << '\n';
            websocket_.request_resnapshot(
                "invalid user data; reconnect required");
            return;
        }

        // Callback exceptions propagate to WebSocketClient, which counts them
        // and requests a reconnect.
        for (const auto &event : events)
        {
            if (!stream_is_current(generation, websocket_generation)) return;
            const auto callbacks = callbacks_snapshot();
            std::visit(
                [this, &callbacks](const auto &payload)
                {
                    using Event = std::decay_t<decltype(payload)>;
                    if constexpr (std::is_same_v<Event, UserOrderEvent>)
                    {
                        order_events_++;
                        if (callbacks->order) callbacks->order(payload);
                    }
                    else
                    {
                        trade_events_++;
                        if (callbacks->trade) callbacks->trade(payload);
                    }
                },
                event);
        }
    }

    uint64_t UserStreamRuntime::order_events() const
    {
        return order_events_.load();
    }

    uint64_t UserStreamRuntime::trade_events() const
    {
        return trade_events_.load();
    }
}
