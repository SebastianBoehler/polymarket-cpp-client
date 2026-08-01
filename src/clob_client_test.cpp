#include "clob_client_test_fixture.hpp"

using namespace polymarket;
using namespace clob_test;

namespace
{
    bool test_public_batches()
    {
        LocalServer server;
        ClobClient client(server.url(), 137);
        server.enqueue(R"([{"asset_id":"A","bids":[{"price":"0.4","size":"2"}],"asks":[]},{"asset_id":"B","bids":[],"asks":[]}])");
        const auto books = client.get_order_books({"A", "B"});
        server.enqueue(R"({"A":{"BUY":"0.41"},"B":{"BUY":0.52}})");
        const auto prices = client.get_prices({"A", "B"}, "buy");
        server.enqueue(R"([{"token_id":"B","price":"0.62","side":"SELL"},{"token_id":"A","price":"0.31","side":"BUY"}])");
        const auto last = client.get_last_trades_prices({"A", "B"});
        server.enqueue(R"({"A":"0.45","B":"0.55"})");
        const auto midpoints = client.get_midpoints({"A", "B"});
        server.enqueue(R"({"A":"0.02","B":"0.03"})");
        const auto spreads = client.get_spreads({"A", "B"});
        const auto requests = server.requests();

        return check(requests.size() == 5, "batch calls did not issue five requests") &&
               check(requests[0].method == "POST" && requests[0].target == "/books", "books must POST /books") &&
               check(requests[0].body == R"([{"token_id":"A"},{"token_id":"B"}])", "books body mismatch") &&
               check(requests[1].method == "POST" && requests[1].target == "/prices", "prices must POST /prices") &&
               check(requests[1].body == R"([{"side":"BUY","token_id":"A"},{"side":"BUY","token_id":"B"}])", "prices body mismatch") &&
               check(requests[2].method == "POST" && requests[2].target == "/last-trades-prices", "last prices route mismatch") &&
               check(requests[3].method == "POST" && requests[3].target == "/midpoints", "midpoints route mismatch") &&
               check(requests[4].method == "POST" && requests[4].target == "/spreads", "spreads route mismatch") &&
               check(books.size() == 2 && books.at("A").bids.size() == 1, "books response shape mismatch") &&
               check(prices.size() == 2 && prices[0].token_id == "A" && prices[0].price == 0.41, "prices response shape mismatch") &&
               check(last.size() == 2 && last[0].token_id == "B" && last[0].price == 0.62, "last prices response mapping mismatch") &&
               check(midpoints.size() == 2 && midpoints[1].token_id == "B" && midpoints[1].mid == 0.55, "midpoints response shape mismatch") &&
               check(spreads.size() == 2 && spreads[0].token_id == "A" && spreads[0].spread == 0.02, "spreads response shape mismatch");
    }

    bool test_authenticated_reads()
    {
        LocalServer server;
        auto client = authenticated_client(server.url());
        server.enqueue(R"({"id":"order-1","owner":"owner","maker_address":"maker","market":"mkt","asset_id":"tok","side":"BUY","original_size":"4","size_matched":"1","price":"0.4","status":"LIVE","associate_trades":["trade-0"],"outcome":"YES","created_at":100,"expiration":"0","order_type":"GTC"})");
        const auto order = client.get_order("order-1");
        server.enqueue(R"({"next_cursor":"N+/=","data":[{"id":"open-1","owner":"owner","maker_address":"maker","market":"mkt","asset_id":"A","side":"BUY","original_size":"1","size_matched":"0","price":"0.4","status":"LIVE","associate_trades":[],"outcome":"YES","created_at":"100","expiration":"0","order_type":"GTC"}]})", 200, 1100);
        server.enqueue(R"({"next_cursor":"LTE=","data":[{"id":"open-2","owner":"owner","maker_address":"maker","market":"mkt","asset_id":"B","side":"SELL","original_size":"2","size_matched":"0","price":"0.6","status":"LIVE","associate_trades":[],"outcome":"NO","created_at":"101","expiration":"0","order_type":"GTC"}]})");
        const auto open_orders = client.get_open_orders("mkt+/=");
        server.enqueue(R"({"next_cursor":"N+/=","data":[{"id":"trade-1","taker_order_id":"taker-1","market":"mkt","asset_id":"A","side":"BUY","size":"1","fee_rate_bps":"0","price":"0.4","status":"CONFIRMED","match_time":"100","last_update":"101","outcome":"YES","bucket_index":0,"owner":"owner","maker_address":"maker","maker_orders":[],"transaction_hash":"0x1","trader_side":"TAKER"}]})", 200, 1100);
        server.enqueue(R"({"next_cursor":"LTE=","data":[{"id":"trade-2","taker_order_id":"taker-2","market":"mkt","asset_id":"B","side":"SELL","size":"2","fee_rate_bps":"0","price":"0.6","status":"CONFIRMED","match_time":"101","last_update":"102","outcome":"NO","bucket_index":1,"owner":"owner","maker_address":"maker","maker_orders":[],"transaction_hash":"0x2","trader_side":"MAKER"}]})");
        const auto trades = client.get_trades();
        const auto requests = server.requests();

        return check(requests.size() == 5, "authenticated reads must follow both cursors") &&
               check(requests[0].target == "/data/order/order-1", "single order route mismatch") &&
               check(requests[1].target == "/data/orders?market=mkt%2B%2F%3D&next_cursor=MA%3D%3D", "open orders first cursor mismatch") &&
               check(requests[2].target == "/data/orders?market=mkt%2B%2F%3D&next_cursor=N%2B%2F%3D", "open orders second cursor mismatch") &&
               check(requests[3].target == "/data/trades?next_cursor=MA%3D%3D", "trades first cursor mismatch") &&
               check(requests[4].target == "/data/trades?next_cursor=N%2B%2F%3D", "trades second cursor mismatch") &&
               check(requests[0].headers.at("poly_signature") == expected_signature(requests[0], "/data/order/order-1"), "single order signature path mismatch") &&
               check(requests[1].headers.at("poly_signature") == expected_signature(requests[1], "/data/orders"), "orders must sign bare endpoint") &&
               check(requests[1].headers.at("poly_timestamp") != requests[2].headers.at("poly_timestamp"), "open orders must regenerate L2 headers per page") &&
               check(requests[3].headers.at("poly_signature") == expected_signature(requests[3], "/data/trades"), "trades must sign bare endpoint") &&
               check(requests[3].headers.at("poly_timestamp") != requests[4].headers.at("poly_timestamp"), "trades must regenerate L2 headers per page") &&
               check(order && order->id == "order-1" && order->created_at == "100", "single order response parsing mismatch") &&
               check(open_orders.size() == 2 && open_orders[1].id == "open-2", "open orders pagination mismatch") &&
               check(trades.size() == 2 && trades[1].id == "trade-2", "trades pagination mismatch");
    }

    bool test_delete_contracts()
    {
        LocalServer server;
        auto client = authenticated_client(server.url());
        server.enqueue(R"({"canceled":["o1"],"not_canceled":{}})");
        const bool one = client.cancel_order("o1");
        server.enqueue(R"({"canceled":["o1","o2"],"not_canceled":{}})");
        const bool many = client.cancel_orders({"o1", "o2"});
        server.enqueue(R"({"canceled":[],"not_canceled":{}})");
        const bool all = client.cancel_all();
        server.enqueue(R"({"canceled":[],"not_canceled":{}})");
        const bool market = client.cancel_market_orders("mkt");
        server.enqueue("OK");
        const bool notifications = client.drop_notifications({"1+/=", "2"});
        server.enqueue("OK");
        const bool api_key = client.delete_api_key();
        const auto requests = server.requests();

        return check(one && many && all && market && notifications && api_key, "DELETE calls should accept HTTP 200") &&
               check(requests.size() == 6, "expected six DELETE requests") &&
               check(requests[0].method == "DELETE" && requests[0].target == "/order" && requests[0].body == R"({"orderID":"o1"})", "single cancel wire contract mismatch") &&
               check(requests[1].method == "DELETE" && requests[1].target == "/orders" && requests[1].body == R"(["o1","o2"])", "batch cancel wire contract mismatch") &&
               check(requests[2].method == "DELETE" && requests[2].target == "/cancel-all" && requests[2].body.empty(), "cancel-all wire contract mismatch") &&
               check(requests[3].method == "DELETE" && requests[3].target == "/cancel-market-orders" && requests[3].body == R"({"market":"mkt"})", "market cancel wire contract mismatch") &&
               check(requests[4].method == "DELETE" && requests[4].target == "/notifications?ids=1%2B%2F%3D,2" && requests[4].body.empty(), "drop notifications wire contract mismatch") &&
               check(requests[5].method == "DELETE" && requests[5].target == "/auth/api-key", "delete API key wire contract mismatch") &&
               check(requests[0].headers.at("poly_signature") == expected_signature(requests[0], "/order", requests[0].body), "single cancel signed body mismatch") &&
               check(requests[1].headers.at("poly_signature") == expected_signature(requests[1], "/orders", requests[1].body), "batch cancel signed body mismatch") &&
               check(requests[3].headers.at("poly_signature") == expected_signature(requests[3], "/cancel-market-orders", requests[3].body), "market cancel signed body mismatch") &&
               check(requests[4].headers.at("poly_signature") == expected_signature(requests[4], "/notifications"), "notifications must sign bare endpoint without query") &&
               check(requests[5].headers.at("poly_signature") == expected_signature(requests[5], "/auth/api-key"), "delete API key signature mismatch");
    }

    bool test_public_current_routes()
    {
        LocalServer server;
        ClobClient client(server.url(), 137);
        server.enqueue(R"({"history":[{"t":101,"p":0.42}]})");
        const auto ranged_history = client.get_prices_history("tok+/=", 100, 200, "1h", "5+/=");
        server.enqueue(R"({"history":[{"t":102,"p":"0.43"}]})");
        const auto relative_history = client.get_prices_history("tok+/=", 0, 0, "1d+/=", "60");
        server.enqueue(R"({"condition_id":"cond","id":7,"question":"Will it?","market_slug":"will-it","event_slug":"event","series_slug":"series","icon":"icon","image":"image","tags":["tag-a","tag-b"]})");
        const auto activity = client.get_market_live_activity("cond");
        server.enqueue(R"({})");
        const auto empty_activity = client.get_market_live_activity("cond");
        server.enqueue(R"({"condition_id":"other","id":7,"question":"Will it?","market_slug":"will-it","event_slug":"event","series_slug":"series","icon":"icon","image":"image","tags":[]})");
        const auto mismatched_activity = client.get_market_live_activity("cond");
        server.enqueue(R"({"condition_id":"cond","id":"7","question":"Will it?","market_slug":"will-it","event_slug":"event","series_slug":"series","icon":"icon","image":"image","tags":[]})");
        const auto mistyped_activity = client.get_market_live_activity("cond");
        server.enqueue(R"({"base_fee":30})");
        const auto fee_rate = client.get_fee_rate("tok+/=");
        const auto empty_fee_rate = client.get_fee_rate("");
        const auto requests = server.requests();

        return check(requests.size() == 7, "expected seven current public route requests") &&
               check(requests[0].target == "/prices-history?market=tok%2B%2F%3D&startTs=100&endTs=200&fidelity=5%2B%2F%3D", "absolute price history query mismatch") &&
               check(requests[1].target == "/prices-history?market=tok%2B%2F%3D&interval=1d%2B%2F%3D&fidelity=60", "relative price history query mismatch") &&
               check(requests[2].method == "GET" && requests[2].target == "/markets/live-activity/cond", "live activity route mismatch") &&
               check(requests[6].method == "GET" && requests[6].target == "/fee-rate?token_id=tok%2B%2F%3D", "fee-rate route mismatch") &&
               check(ranged_history.size() == 1 && ranged_history[0].timestamp == 101 && ranged_history[0].price == 0.42, "numeric price history response mismatch") &&
               check(relative_history.size() == 1 && relative_history[0].price == 0.43, "string price history response mismatch") &&
               check(activity && activity->condition_id == "cond" && activity->tags.size() == 2, "live activity response mismatch") &&
               check(!empty_activity && !mismatched_activity && !mistyped_activity,
                     "live activity must reject empty, mismatched, and mistyped payloads") &&
               check(fee_rate && fee_rate->base_fee == "30", "fee-rate response mismatch") &&
               check(!empty_fee_rate, "fee-rate must reject an empty token without a request");
    }

    bool test_price_history_requires_real_timestamps()
    {
        LocalServer server;
        ClobClient client(server.url(), 137);
        server.enqueue(R"({"history":[{"p":0.4}]})");
        const auto missing = client.get_prices_history("token");
        server.enqueue(R"({"history":[{"t":1,"p":0.4},{"p":0.5}]})");
        const auto partial = client.get_prices_history("token");
        server.enqueue(R"({"history":[{"t":0,"p":0.4}]})");
        const auto zero = client.get_prices_history("token");
        return check(missing.empty() && partial.empty() && zero.empty(),
                     "price history requires nonzero unsigned timestamps atomically");
    }

    bool test_signed_order_scoring_requires_server_order_ids()
    {
        LocalServer server;
        auto client = authenticated_client(server.url());
        SignedOrder order;
        order.salt = "not-a-server-order-id";
        bool single_failed_explicitly = false;
        bool batch_failed_explicitly = false;
        try
        {
            (void)client.is_order_scoring(order);
        }
        catch (const std::invalid_argument &)
        {
            single_failed_explicitly = true;
        }
        try
        {
            (void)client.are_orders_scoring(std::vector<SignedOrder>{order});
        }
        catch (const std::invalid_argument &)
        {
            batch_failed_explicitly = true;
        }

        return check(single_failed_explicitly && batch_failed_explicitly,
                     "SignedOrder scoring overloads must explicitly require server order IDs") &&
               check(server.requests().empty(), "SignedOrder scoring overloads must not query salts");
    }

    bool test_legacy_market_trades_requires_live_activity_api()
    {
        LocalServer server;
        ClobClient client(server.url(), 137);
        bool failed_explicitly = false;
        try
        {
            (void)client.get_market_trades_events("condition-id");
        }
        catch (const std::logic_error &)
        {
            failed_explicitly = true;
        }

        return check(failed_explicitly,
                     "legacy market-trades method must direct callers to live activity API") &&
               check(server.requests().empty(), "legacy market-trades method must not issue obsolete wire requests");
    }

    bool test_market_metadata_fields()
    {
        LocalServer server;
        ClobClient client(server.url(), 137);
        server.enqueue(R"({"next_cursor":"LTE=","data":[{"condition_id":"cond","question":"Question","market_slug":"slug","neg_risk":true,"active":true,"closed":false,"minimum_order_size":5,"minimum_tick_size":"0.0025","end_date_iso":"2026-08-01T05:30:00Z","maker_base_fee":1000,"taker_base_fee":2000,"tokens":[{"token_id":"yes","outcome":"Yes"},{"token_id":"no","outcome":"No"}]}]})");
        const auto markets = client.get_markets();
        server.enqueue(R"({})");
        const auto empty_market = client.get_market("cond");
        server.enqueue(R"({"condition_id":"other","tokens":[{"token_id":"yes","outcome":"Yes"},{"token_id":"no","outcome":"No"}]})");
        const auto mismatched_market = client.get_market("cond");
        server.enqueue(R"({"next_cursor":"N+/=","data":[{"condition_id":"simple","active":true,"closed":false,"tokens":[{"token_id":"yes","outcome":"Yes"},{"token_id":"no","outcome":"No"}]}]})");
        const auto simplified = client.get_simplified_markets("cursor+/=");
        const auto requests = server.requests();
        return check(markets.data.size() == 1 && markets.next_cursor == "LTE=",
                     "current V2 market page and cursor parse") &&
               check(markets.data[0].minimum_order_size == 5.0,
                     "minimum order size must be retained") &&
               check(markets.data[0].minimum_tick_size == "0.0025",
                     "minimum tick size must be retained") &&
               check(markets.data[0].end_time_ms > 0, "market end time must be retained") &&
               check(markets.data[0].fees_enabled &&
                         markets.data[0].maker_base_fee == 1000.0 &&
                         markets.data[0].taker_base_fee == 2000.0 &&
                         markets.data[0].fee_rate == 0.0,
                     "base fees must be retained without inventing a fee curve") &&
               check(!empty_market && !mismatched_market,
                     "single-market responses require the requested market identity") &&
               check(simplified.data.size() == 1 &&
                         simplified.next_cursor == "N+/=",
                     "simplified market cursor must be preserved") &&
               check(requests.back().target ==
                         "/simplified-markets?next_cursor=cursor%2B%2F%3D",
                     "market page cursor must be encoded on the wire");
    }

}

int main()
{
    http_global_init();
    const bool ok = test_public_batches() && test_authenticated_reads() && test_delete_contracts() &&
                    test_public_current_routes() && test_scoring_and_notifications() &&
                    test_rewards_schemas_fail_closed_atomically() &&
                    test_signed_order_scoring_requires_server_order_ids() &&
                    test_price_history_requires_real_timestamps() &&
                    test_legacy_market_trades_requires_live_activity_api() && test_rewards_contracts() &&
                    test_market_metadata_fields() &&
                    test_conditional_balance_allowance_token() &&
                    test_market_order_rejects_resting_types() &&
                    test_market_order_depth_and_fail_closed_metadata() && test_limit_order_fail_closed_metadata() &&
                    test_public_order_price_grid_validation() &&
                    test_metadata_cache_avoids_repeated_round_trips() &&
                    test_metadata_cache_coalesces_concurrent_cold_misses() &&
                    test_metadata_cache_invalidation_refreshes_values() &&
                    test_signer_only_l1_bootstrap_installs_credentials() &&
                    test_credentials_validation() &&
                    test_clob_constructors_reject_unsupported_chains() &&
                    test_clob_constructors_require_non_eoa_funder() &&
                    test_clob_warm_connection_uses_time_only() && test_order_result_schema_failures() &&
                    test_order_post_response_contracts() && test_cancellation_response_contracts() &&
                    test_clob_type_scalar_defaults() && test_pagination_cycle_and_page_limit_guards();
    http_global_cleanup();
    return ok ? 0 : 1;
}
