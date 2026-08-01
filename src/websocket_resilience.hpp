#pragma once

#include "websocket_client.hpp"
#include "types.hpp"
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace polymarket::detail
{
    struct QueuedMessage
    {
        std::string payload;
        uint64_t generation{0};
    };

    enum class QueuePushResult
    {
        Accepted,
        Gap,
        Closed
    };

    struct MarketPriceChange
    {
        bool bid{false};
        PriceLevel level;
    };

    struct MarketBookEvent
    {
        std::string asset_id;
        bool snapshot{false};
        std::vector<PriceLevel> bids;
        std::vector<PriceLevel> asks;
        std::vector<MarketPriceChange> changes;
    };

    struct MarketTickSizeChange
    {
        std::string asset_id;
        std::string new_tick_size;
    };

    class BoundedMessageQueue
    {
    public:
        explicit BoundedMessageQueue(std::size_t limit);

        QueuePushResult push(QueuedMessage message);
        std::optional<QueuedMessage> pop();
        std::size_t discard_pending();
        void record_drop();
        void close();
        void reset(std::size_t limit);
        uint64_t dropped() const;
        std::size_t size() const;

    private:
        mutable std::mutex mutex_;
        std::condition_variable cv_;
        std::deque<QueuedMessage> messages_;
        std::size_t limit_;
        bool closed_{false};
        uint64_t dropped_{0};
    };

    std::optional<TypedWebSocketMessage> parse_typed_message(const std::string &message);
    std::string market_subscription_message(const std::vector<std::string> &asset_ids);
    std::string market_subscription_update_message(const std::vector<std::string> &asset_ids,
                                                   bool subscribe);
    bool reconnect_limit_reached(uint32_t retries, int max_reconnect_attempts);
    std::vector<MarketBookEvent> parse_market_book_events(const std::string &message);
    void apply_market_book_event(Orderbook &book,
                                 const MarketBookEvent &event,
                                 uint64_t received_ns);
    std::optional<MarketTickSizeChange> parse_market_tick_size_change(const std::string &message);
}
