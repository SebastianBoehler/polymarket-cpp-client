#include "websocket_resilience.hpp"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace polymarket::detail
{
    BoundedMessageQueue::BoundedMessageQueue(std::size_t limit)
        : limit_(limit == 0 ? 1 : limit)
    {
    }

    QueuePushResult BoundedMessageQueue::push(QueuedMessage message)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_)
        {
            return QueuePushResult::Closed;
        }
        if (messages_.size() >= limit_)
        {
            dropped_ += messages_.size() + 1;
            messages_.clear();
            return QueuePushResult::Gap;
        }
        messages_.push_back(std::move(message));
        cv_.notify_one();
        return QueuePushResult::Accepted;
    }

    std::optional<QueuedMessage> BoundedMessageQueue::pop()
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

    std::size_t BoundedMessageQueue::discard_pending()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto discarded = messages_.size();
        dropped_ += discarded;
        messages_.clear();
        return discarded;
    }

    void BoundedMessageQueue::record_drop()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++dropped_;
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
        if (typed.asset_id.empty() && parsed.contains("price_changes") &&
            parsed["price_changes"].is_array() && parsed["price_changes"].size() == 1)
        {
            typed.asset_id = parsed["price_changes"][0].value("asset_id", "");
        }
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

    std::string market_subscription_message(const std::vector<std::string> &asset_ids)
    {
        return json{{"assets_ids", asset_ids},
                    {"type", "market"},
                    {"custom_feature_enabled", true}}
            .dump();
    }

    std::string market_subscription_update_message(const std::vector<std::string> &asset_ids,
                                                   bool subscribe)
    {
        json message{{"assets_ids", asset_ids},
                     {"operation", subscribe ? "subscribe" : "unsubscribe"}};
        if (subscribe)
        {
            message["custom_feature_enabled"] = true;
        }
        return message.dump();
    }

    bool reconnect_limit_reached(uint32_t retries, int max_reconnect_attempts)
    {
        return max_reconnect_attempts > 0 &&
               retries >= static_cast<uint32_t>(max_reconnect_attempts);
    }

}
