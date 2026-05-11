#pragma once

#include "websocket_client.hpp"
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <string>

namespace polymarket::detail
{
    class BoundedMessageQueue
    {
    public:
        explicit BoundedMessageQueue(std::size_t limit);

        bool push(std::string message);
        std::optional<std::string> pop();
        void close();
        void reset(std::size_t limit);
        uint64_t dropped() const;
        std::size_t size() const;

    private:
        mutable std::mutex mutex_;
        std::condition_variable cv_;
        std::deque<std::string> messages_;
        std::size_t limit_;
        bool closed_{false};
        uint64_t dropped_{0};
    };

    std::optional<TypedWebSocketMessage> parse_typed_message(const std::string &message);
}
