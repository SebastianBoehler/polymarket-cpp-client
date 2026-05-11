#include "websocket_resilience.hpp"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace polymarket::detail
{
    BoundedMessageQueue::BoundedMessageQueue(std::size_t limit)
        : limit_(limit == 0 ? 1 : limit)
    {
    }

    bool BoundedMessageQueue::push(std::string message)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_)
        {
            return false;
        }
        if (messages_.size() >= limit_)
        {
            messages_.pop_front();
            ++dropped_;
        }
        messages_.push_back(std::move(message));
        cv_.notify_one();
        return true;
    }

    std::optional<std::string> BoundedMessageQueue::pop()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this]()
                 { return closed_ || !messages_.empty(); });
        if (messages_.empty())
        {
            return std::nullopt;
        }
        auto message = std::move(messages_.front());
        messages_.pop_front();
        return message;
    }

    void BoundedMessageQueue::close()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
        cv_.notify_all();
    }

    void BoundedMessageQueue::reset(std::size_t limit)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        limit_ = limit == 0 ? 1 : limit;
        messages_.clear();
        closed_ = false;
        dropped_ = 0;
    }

    uint64_t BoundedMessageQueue::dropped() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return dropped_;
    }

    std::size_t BoundedMessageQueue::size() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return messages_.size();
    }

    std::optional<TypedWebSocketMessage> parse_typed_message(const std::string &message)
    {
        auto parsed = json::parse(message);
        if (!parsed.is_object())
        {
            return std::nullopt;
        }

        TypedWebSocketMessage typed;
        typed.raw = message;
        typed.topic = parsed.value("topic", "");
        typed.type = parsed.value("type", "");
        typed.event_type = parsed.value("event_type", "");
        typed.asset_id = parsed.value("asset_id", "");
        if (parsed.contains("payload"))
        {
            const auto &payload = parsed["payload"];
            typed.payload = payload.is_string() ? payload.get<std::string>() : payload.dump();
            if (payload.is_object() && payload.contains("asset_id"))
            {
                typed.asset_id = payload["asset_id"].get<std::string>();
            }
        }

        if (typed.topic.empty() && typed.type.empty() && typed.event_type.empty())
        {
            return std::nullopt;
        }
        return typed;
    }
}
