#include "websocket_client.hpp"
#include "websocket_resilience.hpp"
#include <algorithm>

namespace polymarket
{
    void WebSocketClient::configure(const WebSocketOptions &options)
    {
        options_ = options;
        apply_options();
        message_queue_->reset(options_.message_queue_limit);
    }

    WebSocketOptions WebSocketClient::options() const
    {
        return options_;
    }

    void WebSocketClient::track_subscription(const std::string &message)
    {
        std::lock_guard<std::mutex> lock(subscriptions_mutex_);
        if (std::find(subscriptions_.begin(), subscriptions_.end(), message) == subscriptions_.end())
        {
            subscriptions_.push_back(message);
        }
    }

    void WebSocketClient::untrack_subscription(const std::string &message)
    {
        std::lock_guard<std::mutex> lock(subscriptions_mutex_);
        subscriptions_.erase(std::remove(subscriptions_.begin(), subscriptions_.end(), message), subscriptions_.end());
    }

    void WebSocketClient::clear_subscriptions()
    {
        std::lock_guard<std::mutex> lock(subscriptions_mutex_);
        subscriptions_.clear();
    }

    std::size_t WebSocketClient::tracked_subscription_count() const
    {
        std::lock_guard<std::mutex> lock(subscriptions_mutex_);
        return subscriptions_.size();
    }

    WebSocketStats WebSocketClient::stats() const
    {
        return {
            messages_received_.load(),
            bytes_received_.load(),
            reconnects_.load(),
            message_queue_->dropped(),
            parse_errors_.load(),
            last_message_time_ns_.load()};
    }

    void WebSocketClient::apply_options()
    {
        const int ping_seconds = std::max(1, options_.ping_interval_ms / 1000);
        ws_.setPingInterval(ping_seconds);
        ws_.setMinWaitBetweenReconnectionRetries(options_.min_backoff_ms);
        ws_.setMaxWaitBetweenReconnectionRetries(options_.max_backoff_ms);
        if (options_.reconnect_enabled)
        {
            ws_.enableAutomaticReconnection();
        }
        else
        {
            ws_.disableAutomaticReconnection();
        }
    }

    void WebSocketClient::start_message_worker()
    {
        if (worker_running_.load())
        {
            return;
        }

        worker_running_.store(true);
        message_worker_ = std::thread([this]()
                                      {
            while (worker_running_.load())
            {
                auto message = message_queue_->pop();
                if (!message)
                {
                    break;
                }
                dispatch_message(*message);
            } });
    }

    void WebSocketClient::stop_message_worker()
    {
        message_queue_->close();
        worker_running_.store(false);
        if (message_worker_.joinable())
        {
            message_worker_.join();
        }
        message_queue_->reset(options_.message_queue_limit);
    }

    void WebSocketClient::enqueue_message(const std::string &message)
    {
        message_queue_->push(message);
    }

    void WebSocketClient::dispatch_message(const std::string &message)
    {
        if (on_message_cb_)
        {
            on_message_cb_(message);
        }

        if (!on_typed_message_cb_)
        {
            return;
        }

        try
        {
            auto typed = detail::parse_typed_message(message);
            if (typed)
            {
                on_typed_message_cb_(*typed);
            }
        }
        catch (...)
        {
            parse_errors_++;
        }
    }

    void WebSocketClient::restore_subscriptions()
    {
        std::vector<std::string> subscriptions;
        {
            std::lock_guard<std::mutex> lock(subscriptions_mutex_);
            subscriptions = subscriptions_;
        }

        for (const auto &message : subscriptions)
        {
            ws_.send(message);
        }
    }
}
