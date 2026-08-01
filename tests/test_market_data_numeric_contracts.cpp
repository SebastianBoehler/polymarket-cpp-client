#include "clob_client.hpp"
#include "clob_client_test_fixture.hpp"
#include "market_fetcher.hpp"

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

using namespace polymarket;

namespace
{
    bool check(bool condition, const char *message)
    {
        if (!condition)
            std::cerr << "failed: " << message << '\n';
        return condition;
    }

    const std::vector<std::string> invalid_books = {
        R"({})",
        R"({"asset_id":"B","bids":[],"asks":[]})",
        R"({"asset_id":"A","bids":[]})",
        R"({"asset_id":"A","asks":[]})",
        R"({"asset_id":"A","bids":[{"price":"0.4tail","size":"1"}],"asks":[]})",
        R"({"asset_id":"A","bids":[{"price":"nan","size":"1"}],"asks":[]})",
        R"({"asset_id":"A","bids":[{"price":"inf","size":"1"}],"asks":[]})",
        R"({"asset_id":"A","bids":[{"price":0,"size":"1"}],"asks":[]})",
        R"({"asset_id":"A","bids":[{"price":1,"size":"1"}],"asks":[]})",
        R"({"asset_id":"A","bids":[{"price":-0.1,"size":"1"}],"asks":[]})",
        R"({"asset_id":"A","bids":[{"price":1.1,"size":"1"}],"asks":[]})",
        R"({"asset_id":"A","bids":[{"price":"0.4","size":"1tail"}],"asks":[]})",
        R"({"asset_id":"A","bids":[{"price":"0.4","size":"nan"}],"asks":[]})",
        R"({"asset_id":"A","bids":[{"price":"0.4","size":"inf"}],"asks":[]})",
        R"({"asset_id":"A","bids":[{"price":"0.4","size":-1}],"asks":[]})"};

    bool test_clob_orderbook_numbers()
    {
        clob_test::LocalServer server;
        ClobClient client(server.url(), 137);
        server.enqueue(
            R"({"asset_id":"A","bids":[{"price":"0.40","size":2}],"asks":[{"price":0.60,"size":"3"}]})");
        const auto valid = client.get_order_book("A");
        if (!check(valid && valid->bids.size() == 1 && valid->asks.size() == 1 &&
                       valid->bids[0].price == 0.4 && valid->bids[0].size == 2.0 &&
                       valid->asks[0].price == 0.6 && valid->asks[0].size == 3.0,
                   "CLOB book accepts canonical string and numeric fields"))
            return false;

        for (const auto &body : invalid_books)
        {
            server.enqueue(body);
            if (!check(!client.get_order_book("A"),
                       "CLOB book rejects malformed or out-of-range level"))
                return false;
        }
        return true;
    }

    bool test_clob_batch_orderbook_identity_is_atomic()
    {
        clob_test::LocalServer server;
        ClobClient client(server.url(), 137);
        for (const auto *body : {
                 R"([{"asset_id":"A","bids":[],"asks":[]}])",
                 R"([{"asset_id":"A","bids":[],"asks":[]},{"asset_id":"A","bids":[],"asks":[]}])",
                 R"([{"asset_id":"A","bids":[],"asks":[]},{"asset_id":"C","bids":[],"asks":[]}])",
                 R"([{"asset_id":"A","bids":[],"asks":[]},{"asset_id":"B","bids":[]}])"})
        {
            server.enqueue(body);
            if (!check(client.get_order_books({"A", "B"}).empty(),
                       "batch books reject missing, duplicate, unexpected, or malformed identities"))
            {
                return false;
            }
        }
        return true;
    }

    bool test_market_fetcher_orderbook_numbers()
    {
        clob_test::LocalServer server;
        Config config;
        config.clob_rest_url = server.url();
        MarketFetcher fetcher(config);
        server.enqueue(
            R"({"asset_id":"A","bids":[{"price":"0.40","size":2}],"asks":[{"price":0.60,"size":"3"}]})");
        const auto valid = fetcher.fetch_orderbook("A");
        if (!check(valid && valid->bids.size() == 1 && valid->asks.size() == 1 &&
                       valid->bids[0].price == 0.4 && valid->asks[0].price == 0.6,
                   "MarketFetcher book accepts canonical string and numeric fields"))
            return false;

        for (const auto &body : invalid_books)
        {
            server.enqueue(body);
            if (!check(!fetcher.fetch_orderbook("A"),
                       "MarketFetcher rejects malformed or out-of-range level"))
                return false;
        }
        return true;
    }

    bool test_scalar_market_data_numbers()
    {
        clob_test::LocalServer server;
        ClobClient client(server.url(), 137);
        server.enqueue(R"({"price":0.42})");
        const auto price = client.get_price("A");
        server.enqueue(R"({"price":"0.43"})");
        const auto last = client.get_last_trade_price("A");
        server.enqueue(R"({"mid":0.50})");
        const auto midpoint = client.get_midpoint("A");
        server.enqueue(R"({"spread":"0.03"})");
        const auto spread = client.get_spread("A");
        if (!check(price && price->price == 0.42 && last && last->price == 0.43 &&
                       midpoint && midpoint->mid == 0.5 && spread && spread->spread == 0.03,
                   "scalar endpoints accept canonical string and numeric fields"))
            return false;

        server.enqueue(R"({"price":"0.4tail"})");
        const auto trailing_price = client.get_price("A");
        server.enqueue(R"({"price":"nan"})");
        const auto nan_last = client.get_last_trade_price("A");
        server.enqueue(R"({"mid":"inf"})");
        const auto infinite_midpoint = client.get_midpoint("A");
        server.enqueue(R"({"spread":-0.01})");
        const auto negative_spread = client.get_spread("A");
        server.enqueue(R"({"price":1.01})");
        const auto high_price = client.get_price("A");
        server.enqueue(R"({"mid":-0.01})");
        const auto negative_midpoint = client.get_midpoint("A");
        server.enqueue(R"({"spread":1.01})");
        const auto high_spread = client.get_spread("A");
        return check(!trailing_price && !nan_last && !infinite_midpoint &&
                         !negative_spread && !high_price && !negative_midpoint && !high_spread,
                     "scalar endpoints reject malformed, non-finite, and out-of-range data");
    }

    bool test_batch_market_data_is_atomic()
    {
        clob_test::LocalServer server;
        ClobClient client(server.url(), 137);
        server.enqueue(R"({"A":{"BUY":"0.4"},"B":{"BUY":"0.5tail"}})");
        const auto prices = client.get_prices({"A", "B"}, "buy");
        server.enqueue(
            R"([{"token_id":"A","price":0.4},{"token_id":"B","price":"nan"}])");
        const auto last = client.get_last_trades_prices({"A", "B"});
        server.enqueue(R"({"A":0.4,"B":"inf"})");
        const auto midpoints = client.get_midpoints({"A", "B"});
        server.enqueue(R"({"A":0.02,"B":-0.1})");
        const auto spreads = client.get_spreads({"A", "B"});
        server.enqueue(R"({"history":[{"t":1,"p":0.4},{"t":2,"p":"0.5tail"}]})");
        const auto history = client.get_prices_history("A");
        return check(prices.empty() && last.empty() && midpoints.empty() &&
                         spreads.empty() && history.empty(),
                     "batch and history endpoints return no partial invalid data");
    }

    bool test_tick_size_numbers()
    {
        clob_test::LocalServer server;
        ClobClient client(server.url(), 137);
        server.enqueue(R"({"minimum_tick_size":0.01})");
        const auto numeric = client.get_tick_size("numeric");
        server.enqueue(R"({"minimum_tick_size":"nan"})");
        const auto nan = client.get_tick_size("nan");
        server.enqueue(R"({"minimum_tick_size":"0.01tail"})");
        const auto trailing = client.get_tick_size("trailing");
        return check(numeric && numeric->minimum_tick_size == "0.01" && !nan && !trailing,
                     "tick size accepts canonical numbers and rejects invalid strings");
    }
}

int main()
{
    http_global_init();
    const bool ok = test_clob_orderbook_numbers() &&
                    test_clob_batch_orderbook_identity_is_atomic() &&
                    test_market_fetcher_orderbook_numbers() &&
                    test_scalar_market_data_numbers() &&
                    test_batch_market_data_is_atomic() &&
                    test_tick_size_numbers();
    http_global_cleanup();
    return ok ? 0 : 1;
}
