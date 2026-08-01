#include "orderbook.hpp"
#include "orderbook_runtime.hpp"

namespace polymarket
{
    OrderbookManager::OrderbookManager(const Config &config)
        : runtime_(detail::OrderbookRuntime::create(config))
    {
    }

    OrderbookManager::~OrderbookManager()
    {
        auto runtime = std::move(runtime_);
        if (runtime) runtime->shutdown();
    }

    void OrderbookManager::subscribe(const std::vector<MarketState> &markets)
    {
        auto runtime = runtime_;
        if (runtime) runtime->subscribe(markets);
    }

    void OrderbookManager::subscribe(const MarketState &market)
    {
        auto runtime = runtime_;
        if (runtime) runtime->subscribe(market);
    }

    void OrderbookManager::unsubscribe(const std::string &token_id)
    {
        auto runtime = runtime_;
        if (runtime) runtime->unsubscribe(token_id);
    }

    void OrderbookManager::unsubscribe_all()
    {
        auto runtime = runtime_;
        if (runtime) runtime->unsubscribe_all();
    }

    std::optional<Orderbook> OrderbookManager::get_orderbook(
        const std::string &token_id) const
    {
        auto runtime = runtime_;
        return runtime ? runtime->get_orderbook(token_id) : std::nullopt;
    }

    MarketState OrderbookManager::get_market(const std::string &condition_id) const
    {
        auto runtime = runtime_;
        return runtime ? runtime->get_market(condition_id) : MarketState{};
    }

    void OrderbookManager::on_orderbook_update(OrderbookUpdateCallback callback)
    {
        auto runtime = runtime_;
        if (runtime) runtime->on_orderbook_update(std::move(callback));
    }

    void OrderbookManager::on_arb_opportunity(ArbOpportunityCallback callback)
    {
        auto runtime = runtime_;
        if (runtime) runtime->on_arb_opportunity(std::move(callback));
    }

    void OrderbookManager::on_arb_opportunity_snapshot(
        ArbOpportunitySnapshotCallback callback)
    {
        auto runtime = runtime_;
        if (runtime) runtime->on_arb_opportunity_snapshot(std::move(callback));
    }

    void OrderbookManager::on_arb_opportunity_with_permit(
        ArbOpportunityPermitCallback callback)
    {
        auto runtime = runtime_;
        if (runtime) runtime->on_arb_opportunity_with_permit(std::move(callback));
    }

    bool OrderbookManager::is_stream_current(StreamGenerationPermit permit) const
    {
        auto runtime = runtime_;
        return runtime && runtime->is_stream_current(permit);
    }

    bool OrderbookManager::connect()
    {
        auto runtime = runtime_;
        return runtime && runtime->connect();
    }

    void OrderbookManager::disconnect()
    {
        auto runtime = runtime_;
        if (runtime) runtime->disconnect();
    }

    bool OrderbookManager::is_connected() const
    {
        auto runtime = runtime_;
        return runtime && runtime->is_connected();
    }

    void OrderbookManager::run()
    {
        auto runtime = runtime_;
        if (runtime) runtime->run();
    }

    void OrderbookManager::stop()
    {
        auto runtime = runtime_;
        if (runtime) runtime->stop();
    }

    uint64_t OrderbookManager::total_updates() const
    {
        auto runtime = runtime_;
        return runtime ? runtime->total_updates() : 0;
    }

    uint64_t OrderbookManager::arb_opportunities() const
    {
        auto runtime = runtime_;
        return runtime ? runtime->arb_opportunities() : 0;
    }
}
