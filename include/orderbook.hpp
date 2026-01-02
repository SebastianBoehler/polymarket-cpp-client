#pragma once

#include "types.hpp"
#include "websocket_client.hpp"
#include <unordered_map>
#include <shared_mutex>
#include <functional>

namespace polymarket
{

    // Callback for orderbook updates
    using OrderbookUpdateCallback = std::function<void(const std::string &asset_id, const Orderbook &book)>;
    using ArbOpportunityCallback = std::function<void(const LiveMarketState &market, double combined)>;

    // Orderbook manager - subscribes to WebSocket and maintains orderbook state
    class OrderbookManager
    {
    public:
        explicit OrderbookManager(const Config &config);
        ~OrderbookManager();

        // Subscribe to markets
        void subscribe(const std::vector<MarketState> &markets);
        void subscribe(const MarketState &market);
        void unsubscribe(const std::string &token_id);
        void unsubscribe_all();

        // Get current orderbook
        std::optional<Orderbook> get_orderbook(const std::string &token_id) const;

        // Get market state (returns empty MarketState if not found)
        MarketState get_market(const std::string &condition_id) const;

        // Callbacks
        void on_orderbook_update(OrderbookUpdateCallback callback);
        void on_arb_opportunity(ArbOpportunityCallback callback);

        // Connection
        bool connect();
        void disconnect();
        bool is_connected() const;

        // Run event loop (blocking)
        void run();

        // Stop
        void stop();

        // Statistics
        uint64_t total_updates() const { return total_updates_.load(); }
        uint64_t arb_opportunities() const { return arb_opportunities_.load(); }

    private:
        Config config_;
        WebSocketClient ws_;

        // Orderbooks by token_id
        mutable std::shared_mutex orderbooks_mutex_;
        std::unordered_map<std::string, Orderbook> orderbooks_;

        // Markets by condition_id (using unique_ptr for non-copyable LiveMarketState)
        mutable std::shared_mutex markets_mutex_;
        std::unordered_map<std::string, std::unique_ptr<LiveMarketState>> markets_;

        // Token to condition mapping
        std::unordered_map<std::string, std::string> token_to_condition_;

        // Subscribed tokens
        std::vector<std::string> subscribed_tokens_;

        // Callbacks
        OrderbookUpdateCallback on_update_cb_;
        ArbOpportunityCallback on_arb_cb_;

        // Statistics
        std::atomic<uint64_t> total_updates_{0};
        std::atomic<uint64_t> arb_opportunities_{0};

        // Internal methods
        void handle_message(const std::string &message);
        void handle_orderbook_update(const std::string &asset_id, const Orderbook &book);
        void send_subscribe_message();
        void check_arb_opportunity(const std::string &condition_id);
    };

} // namespace polymarket
