#pragma once

#include "orderbook.hpp"
#include "websocket_client.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace polymarket::detail
{
    struct MarketBookEvent;

    struct OrderbookCallbacks
    {
        OrderbookUpdateCallback update;
        ArbOpportunityCallback arb;
        ArbOpportunitySnapshotCallback arb_snapshot;
        ArbOpportunityPermitCallback arb_permit;
    };

    class OrderbookRuntime final
        : public std::enable_shared_from_this<OrderbookRuntime>
    {
    public:
        static std::shared_ptr<OrderbookRuntime> create(const Config &config);
        ~OrderbookRuntime();

        void shutdown();
        void subscribe(const std::vector<MarketState> &markets);
        void subscribe(const MarketState &market);
        void unsubscribe(const std::string &token_id);
        void unsubscribe_all();
        std::optional<Orderbook> get_orderbook(const std::string &token_id) const;
        MarketState get_market(const std::string &condition_id) const;

        void on_orderbook_update(OrderbookUpdateCallback callback);
        void on_arb_opportunity(ArbOpportunityCallback callback);
        void on_arb_opportunity_snapshot(ArbOpportunitySnapshotCallback callback);
        void on_arb_opportunity_with_permit(ArbOpportunityPermitCallback callback);

        bool is_stream_current(StreamGenerationPermit permit) const;
        bool connect();
        void disconnect();
        bool is_connected() const;
        void run();
        void stop();
        uint64_t total_updates() const;
        uint64_t arb_opportunities() const;

    private:
        explicit OrderbookRuntime(const Config &config);
        void bind_websocket_callbacks();

        void handle_message(const std::string &message,
                            uint64_t websocket_generation);
        void handle_orderbook_update(const MarketBookEvent &event,
                                     uint64_t received_ns,
                                     uint64_t expected_generation,
                                     uint64_t expected_websocket_generation);
        void check_arb_opportunity(const std::string &condition_id,
                                   uint64_t expected_generation,
                                   uint64_t expected_websocket_generation);
        void invalidate_stream_state();
        void invalidate_cached_state();
        void deactivate_stream();
        bool stream_is_current(uint64_t orderbook_generation,
                               uint64_t websocket_generation) const;

        template <typename Update>
        void update_callbacks(Update &&update)
        {
            std::lock_guard lock(callback_update_mutex_);
            if (!owner_active_.load(std::memory_order_acquire)) return;
            auto next = std::make_shared<OrderbookCallbacks>(*callbacks_snapshot());
            update(*next);
            std::shared_ptr<const OrderbookCallbacks> immutable = std::move(next);
            std::atomic_store_explicit(&callbacks_, std::move(immutable),
                                       std::memory_order_release);
        }

        std::shared_ptr<const OrderbookCallbacks> callbacks_snapshot() const
        {
            return std::atomic_load_explicit(&callbacks_, std::memory_order_acquire);
        }

        void clear_callbacks();

        Config config_;
        WebSocketClient websocket_;

        mutable std::shared_mutex orderbooks_mutex_;
        std::unordered_map<std::string, Orderbook> orderbooks_;
        std::unordered_set<std::string> snapshot_ready_;

        mutable std::shared_mutex markets_mutex_;
        std::unordered_map<std::string, std::shared_ptr<LiveMarketState>> markets_;

        mutable std::mutex subscriptions_mutex_;
        std::unordered_map<std::string, std::string> token_to_condition_;
        std::vector<std::string> subscribed_tokens_;

        mutable std::mutex callback_update_mutex_;
        std::shared_ptr<const OrderbookCallbacks> callbacks_{
            std::make_shared<OrderbookCallbacks>()};

        std::atomic<bool> owner_active_{true};
        std::atomic<bool> stream_active_{false};
        std::atomic<bool> shutdown_started_{false};
        std::atomic<uint64_t> total_updates_{0};
        std::atomic<uint64_t> arb_opportunities_{0};
        std::atomic<uint64_t> stream_generation_{0};
    };
}
