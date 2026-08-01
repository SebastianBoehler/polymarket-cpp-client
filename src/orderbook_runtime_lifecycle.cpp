#include "orderbook_runtime.hpp"
#include "orderbook_stream.hpp"

#include <chrono>

namespace polymarket::detail
{
    bool OrderbookRuntime::stream_is_current(
        uint64_t orderbook_generation,
        uint64_t websocket_generation) const
    {
        return owner_active_.load(std::memory_order_acquire) &&
               stream_active_.load(std::memory_order_acquire) &&
               orderbook_generation == stream_generation_.load() &&
               websocket_generation == websocket_.stream_generation();
    }

    bool OrderbookRuntime::is_stream_current(StreamGenerationPermit permit) const
    {
        return stream_is_current(permit.orderbook_generation,
                                 permit.websocket_generation);
    }

    bool OrderbookRuntime::connect()
    {
        if (!owner_active_.load(std::memory_order_acquire)) return false;
        stream_active_.store(true, std::memory_order_release);
        const bool connected = websocket_.connect() &&
                               websocket_.wait_until_connected(
                                   std::chrono::milliseconds(
                                       config_.ws_connect_timeout_ms));
        if (!connected) deactivate_stream();
        return connected;
    }

    void OrderbookRuntime::disconnect()
    {
        deactivate_stream();
        websocket_.disconnect();
    }

    bool OrderbookRuntime::is_connected() const
    {
        return owner_active_.load(std::memory_order_acquire) &&
               websocket_.is_connected();
    }

    void OrderbookRuntime::run()
    {
        if (owner_active_.load(std::memory_order_acquire)) websocket_.run();
    }

    void OrderbookRuntime::stop()
    {
        deactivate_stream();
        websocket_.stop();
    }

    void OrderbookRuntime::deactivate_stream()
    {
        if (stream_active_.exchange(false, std::memory_order_acq_rel))
            stream_generation_.fetch_add(1);
        invalidate_cached_state();
    }

    void OrderbookRuntime::invalidate_stream_state()
    {
        if (!owner_active_.load(std::memory_order_acquire) ||
            !stream_active_.load(std::memory_order_acquire))
            return;
        stream_generation_.fetch_add(1);
        invalidate_cached_state();
    }

    void OrderbookRuntime::invalidate_cached_state()
    {
        {
            std::unique_lock lock(orderbooks_mutex_);
            invalidate_stream_books(orderbooks_, snapshot_ready_);
        }
        std::unique_lock lock(markets_mutex_);
        for (auto &[_, market] : markets_)
        {
            reset_market_leg(*market, market->token_yes);
            reset_market_leg(*market, market->token_no);
        }
    }
}
