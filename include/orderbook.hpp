#pragma once

#include "types.hpp"
#include <functional>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace polymarket
{
    namespace detail
    {
        struct MarketBookEvent;
        class OrderbookRuntime;
    }

    // Callback for orderbook updates
    using OrderbookUpdateCallback = std::function<void(const std::string &asset_id, const Orderbook &book)>;
    using ArbOpportunityCallback = std::function<void(const LiveMarketState &market, double combined)>;
    using ArbOpportunitySnapshotCallback = std::function<void(const MarketState &market, double combined)>;

    struct StreamGenerationPermit
    {
        uint64_t orderbook_generation{0};
        uint64_t websocket_generation{0};
    };

    using ArbOpportunityPermitCallback =
        std::function<void(const MarketState &market, double combined,
                           StreamGenerationPermit permit)>;

    struct ArbSizingInput
    {
        double yes_ask{0.0};
        double no_ask{0.0};
        double yes_available{0.0};
        double no_available{0.0};
        double max_usdc_per_leg{0.0};
        double slippage{0.0};
        double tick_size{0.0};
        double minimum_order_size{0.0};
        double fee_rate{0.0};
        int fee_exponent{1};
    };

    struct ArbExecutionPlan
    {
        bool executable{false};
        std::string reason;
        double yes_limit_price{0.0};
        double no_limit_price{0.0};
        double yes_shares{0.0};
        double no_shares{0.0};
        double yes_fee{0.0};
        double no_fee{0.0};
        double total_cost{0.0};
        double edge{0.0};
    };

    ArbExecutionPlan size_complementary_arb(const ArbSizingInput &input);
    bool has_fresh_arb_depth(const MarketState &market,
                             uint64_t now,
                             uint64_t max_age,
                             double required_shares);

    // Orderbook manager - subscribes to WebSocket and maintains orderbook state
    class OrderbookManager
    {
    public:
        explicit OrderbookManager(const Config &config);
        ~OrderbookManager();

        OrderbookManager(const OrderbookManager &) = delete;
        OrderbookManager &operator=(const OrderbookManager &) = delete;

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
        void on_arb_opportunity_snapshot(ArbOpportunitySnapshotCallback callback);
        void on_arb_opportunity_with_permit(ArbOpportunityPermitCallback callback);
        bool is_stream_current(StreamGenerationPermit permit) const;

        // Connection
        bool connect();
        void disconnect();
        bool is_connected() const;

        // Run event loop (blocking)
        void run();

        // Stop
        void stop();

        // Statistics
        uint64_t total_updates() const;
        uint64_t arb_opportunities() const;

    private:
        std::shared_ptr<detail::OrderbookRuntime> runtime_;
    };

} // namespace polymarket
