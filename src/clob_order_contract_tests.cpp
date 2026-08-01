#include "clob_client_test_fixture.hpp"

#include <algorithm>
#include <array>

namespace clob_test
{
    bool test_market_order_rejects_resting_types()
    {
        LocalServer server;
        auto client = authenticated_client(server.url());
        CreateMarketOrderParams params;
        params.token_id = "token";
        params.amount = 1.0;
        params.price = 0.5;
        params.tick_size = "0.01";
        params.neg_risk = false;
        const auto gtc = client.create_market_order_result(params, OrderType::GTC);
        const auto gtd = client.create_market_order_result(params, OrderType::GTD);
        return check(!gtc && !gtd &&
                         gtc.error().code == SdkErrorCode::InvalidArgument &&
                         gtd.error().code == SdkErrorCode::InvalidArgument,
                     "market-order helpers must reject resting order types") &&
               check(server.requests().empty(),
                     "invalid market-order types must fail before network I/O");
    }

    bool test_market_order_depth_and_fail_closed_metadata()
    {
        LocalServer server;
        auto client = authenticated_client(server.url());
        CreateMarketOrderParams params{"123", 2.0, OrderSide::BUY, std::nullopt, "0.01"};
        params.neg_risk = true;
        const std::string book = R"({"asset_id":"123","bids":[],"asks":[{"price":"0.70","size":"1"},{"price":"0.60","size":"3"},{"price":"0.50","size":"2"}]})";
        server.enqueue(R"({"minimum_tick_size":"0.01"})");
        server.enqueue(book);
        const auto fok = client.create_market_order_result(params, OrderType::FOK);
        params.amount = 9.0;
        server.enqueue(book);
        const auto fak = client.create_market_order_result(params, OrderType::FAK);

        params.amount = 1.0;
        server.enqueue(R"({"error":"no book"})", 404);
        const auto missing_quote = client.create_market_order_result(params, OrderType::FOK);
        params.price = 0.4;
        params.neg_risk.reset();
        server.enqueue(R"({"error":"no metadata"})", 500);
        const auto missing_neg_risk = client.create_market_order_result(params, OrderType::FOK);
        params.token_id = "456";
        params.neg_risk = false;
        server.enqueue(R"({"error":"no tick"})", 500);
        const auto missing_tick = client.create_market_order_result(params, OrderType::FOK);
        const auto requests = server.requests();

        return check(fok && fok.value().order.maker_amount == "1999998" && fok.value().order.taker_amount == "3333330", "FOK must use an exact executable worst-depth ratio") &&
               check(fak && fak.value().order.maker_amount == "8999998" && fak.value().order.taker_amount == "12857140", "FAK must use an exact shallow-book limit ratio") &&
               check(!missing_quote && missing_quote.error().code == SdkErrorCode::HttpTransport,
                     "missing market quote must be a metadata transport failure") &&
               check(!missing_neg_risk && missing_neg_risk.error().code == SdkErrorCode::HttpTransport,
                     "missing neg-risk metadata must be a metadata transport failure") &&
               check(!missing_tick && missing_tick.error().code == SdkErrorCode::HttpTransport,
                     "missing tick metadata must be a metadata transport failure") &&
               check(requests.size() == 6, "market-order metadata request count mismatch") &&
               check(requests[0].target == "/tick-size?token_id=123" && requests[1].target == "/book?token_id=123", "FOK must fetch tick and full book") &&
               check(requests[2].target == "/book?token_id=123", "FAK must reuse cached tick metadata") &&
               check(requests[3].target == "/book?token_id=123", "missing quote path mismatch") &&
               check(requests[4].target == "/neg-risk?token_id=123", "neg-risk lookup path mismatch") &&
               check(requests[5].target == "/tick-size?token_id=456", "tick failure should stop before signing");
    }

    bool test_limit_order_fail_closed_metadata()
    {
        LocalServer server;
        auto client = authenticated_client(server.url());
        CreateOrderParams params;
        params.token_id = "123";
        params.price = 0.505;
        params.size = 8.048586;
        params.side = OrderSide::BUY;
        params.tick_size.clear();
        params.neg_risk = false;

        server.enqueue(R"({"minimum_tick_size":"0.005"})");
        const auto resolved_tick = client.create_order_result(params);
        params.tick_size = "0.0025";
        const auto too_small_tick = client.create_order_result(params);
        params.tick_size.clear();
        params.token_id = "missing-tick";
        server.enqueue(R"({"error":"no tick"})", 500);
        const auto missing_tick = client.create_order_result(params);
        params.token_id = "malformed-tick";
        server.enqueue(R"({})");
        const auto malformed_tick = client.create_order_result(params);

        params.token_id = "missing-neg";
        params.tick_size = "0.01";
        params.price = 0.50;
        params.neg_risk.reset();
        server.enqueue(R"({"minimum_tick_size":"0.01"})");
        server.enqueue(R"({"error":"no metadata"})", 500);
        const auto missing_neg_risk = client.create_order_result(params);
        params.token_id = "malformed-neg";
        server.enqueue(R"({"minimum_tick_size":"0.01"})");
        server.enqueue(R"({})");
        const auto malformed_neg_risk = client.create_order_result(params);
        const auto requests = server.requests();

        return check(resolved_tick && resolved_tick.value().maker_amount == "4060200" &&
                         resolved_tick.value().taker_amount == "8040000",
                     "empty tick must resolve to the market minimum (maker=" +
                         (resolved_tick ? resolved_tick.value().maker_amount : "error") + ")") &&
               check(!too_small_tick && too_small_tick.error().code == SdkErrorCode::InvalidArgument,
                     "caller tick below the market minimum must be InvalidArgument") &&
               check(!missing_tick && missing_tick.error().code == SdkErrorCode::HttpTransport &&
                         !malformed_tick && malformed_tick.error().code == SdkErrorCode::HttpTransport,
                     "missing or malformed tick metadata must be transport failures") &&
               check(!missing_neg_risk && missing_neg_risk.error().code == SdkErrorCode::HttpTransport &&
                         !malformed_neg_risk && malformed_neg_risk.error().code == SdkErrorCode::HttpTransport,
                     "missing or malformed neg-risk metadata must be transport failures") &&
               check(requests.size() == 7, "limit-order metadata request count mismatch") &&
               check(requests[0].target == "/tick-size?token_id=123" &&
                         requests[1].target == "/tick-size?token_id=missing-tick" &&
                         requests[2].target == "/tick-size?token_id=malformed-tick",
                     "limit orders must resolve uncached tick metadata before signing") &&
               check(requests[3].target == "/tick-size?token_id=missing-neg" && requests[4].target == "/neg-risk?token_id=missing-neg" &&
                         requests[5].target == "/tick-size?token_id=malformed-neg" && requests[6].target == "/neg-risk?token_id=malformed-neg",
                     "limit orders must resolve neg-risk metadata after tick validation");
    }

    bool test_metadata_cache_avoids_repeated_round_trips()
    {
        LocalServer server;
        ClobClient client(server.url(), 137);
        server.enqueue(R"({"minimum_tick_size":"0.005"})");
        const auto first_tick = client.get_tick_size("tok");
        const auto cached_tick = client.get_tick_size("tok");
        server.enqueue(R"({"neg_risk":true})");
        const auto first_neg_risk = client.get_neg_risk("tok");
        const auto cached_neg_risk = client.get_neg_risk("tok");
        const auto requests = server.requests();

        return check(first_tick && cached_tick && cached_tick->minimum_tick_size == "0.005",
                     "tick metadata cache lost its value") &&
               check(first_neg_risk && cached_neg_risk && cached_neg_risk->neg_risk,
                     "neg-risk metadata cache lost its value") &&
               check(requests.size() == 2, "repeated metadata lookups must avoid HTTP round trips") &&
               check(requests[0].target == "/tick-size?token_id=tok" &&
                         requests[1].target == "/neg-risk?token_id=tok",
                     "metadata cache request paths mismatch");
    }

    bool test_metadata_cache_coalesces_concurrent_cold_misses()
    {
        constexpr std::size_t worker_count = 4;
        LocalServer server;
        ClobClient client(server.url(), 137);

        server.enqueue(R"({"minimum_tick_size":"0.005"})", 200, 100);
        std::array<std::optional<TickSizeInfo>, worker_count> ticks;
        std::array<std::thread, worker_count> tick_workers;
        std::atomic<std::size_t> ready{0};
        std::atomic<bool> start{false};
        for (std::size_t i = 0; i < worker_count; ++i)
        {
            tick_workers[i] = std::thread([&, i]
                                          {
                                              ready.fetch_add(1);
                                              while (!start.load())
                                                  std::this_thread::yield();
                                              ticks[i] = client.get_tick_size("cold-tick");
                                          });
        }
        while (ready.load() != worker_count)
            std::this_thread::yield();
        start.store(true);
        for (auto &worker : tick_workers)
            worker.join();

        server.enqueue(R"({"neg_risk":true})", 200, 100);
        std::array<std::optional<NegRiskInfo>, worker_count> neg_risks;
        std::array<std::thread, worker_count> neg_risk_workers;
        ready.store(0);
        start.store(false);
        for (std::size_t i = 0; i < worker_count; ++i)
        {
            neg_risk_workers[i] = std::thread([&, i]
                                              {
                                                  ready.fetch_add(1);
                                                  while (!start.load())
                                                      std::this_thread::yield();
                                                  neg_risks[i] = client.get_neg_risk("cold-neg-risk");
                                              });
        }
        while (ready.load() != worker_count)
            std::this_thread::yield();
        start.store(true);
        for (auto &worker : neg_risk_workers)
            worker.join();

        const bool all_ticks = std::all_of(ticks.begin(), ticks.end(), [](const auto &tick)
                                           { return tick && tick->minimum_tick_size == "0.005"; });
        const bool all_neg_risks = std::all_of(neg_risks.begin(), neg_risks.end(), [](const auto &neg_risk)
                                               { return neg_risk && neg_risk->neg_risk; });
        const auto requests = server.requests();
        return check(all_ticks && all_neg_risks, "coalesced metadata waiters must receive the fetched value") &&
               check(requests.size() == 2, "concurrent cold misses must coalesce to one request per key") &&
               check(requests[0].target == "/tick-size?token_id=cold-tick" &&
                         requests[1].target == "/neg-risk?token_id=cold-neg-risk",
                     "coalesced metadata request paths mismatch");
    }

    bool test_metadata_cache_invalidation_refreshes_values()
    {
        LocalServer server;
        ClobClient client(server.url(), 137);
        server.enqueue(R"({"minimum_tick_size":"0.005"})");
        server.enqueue(R"({"neg_risk":false})");
        const auto initial_tick = client.get_tick_size("refresh-token");
        const auto initial_neg_risk = client.get_neg_risk("refresh-token");

        client.clear_market_metadata_cache("refresh-token");
        server.enqueue(R"({"minimum_tick_size":"0.01"})");
        server.enqueue(R"({"neg_risk":true})");
        const auto refreshed_tick = client.get_tick_size("refresh-token");
        const auto refreshed_neg_risk = client.get_neg_risk("refresh-token");
        const auto requests = server.requests();

        return check(initial_tick && initial_tick->minimum_tick_size == "0.005" &&
                         initial_neg_risk && !initial_neg_risk->neg_risk,
                     "initial metadata values mismatch") &&
               check(refreshed_tick && refreshed_tick->minimum_tick_size == "0.01" &&
                         refreshed_neg_risk && refreshed_neg_risk->neg_risk,
                     "invalidated metadata must be refreshed") &&
               check(requests.size() == 4, "metadata invalidation must trigger fresh HTTP requests");
    }

    bool test_clob_type_scalar_defaults()
    {
        OrderResponse response;
        PriceInfo price;
        MidpointInfo midpoint;
        SpreadInfo spread;
        NegRiskInfo neg_risk;
        OrderScoringResult scoring;
        CreateOrderParams limit_order;
        CreateMarketOrderParams market_order;
        BatchOrderEntry batch_entry{SignedOrder{}, OrderType::GTC};
        PriceHistoryPoint history;
        LiveActivityMarket activity;
        Position position;
        ClobClient::FeeRateInfo legacy_fee{"maker", "taker"};

        return check(!response.success && price.price == 0.0 && midpoint.mid == 0.0 && spread.spread == 0.0,
                     "public response scalars must default to zero") &&
               check(!neg_risk.neg_risk && !scoring.scoring, "public metadata booleans must default false") &&
               check(limit_order.price == 0.0 && limit_order.size == 0.0 && limit_order.side == OrderSide::BUY &&
                         market_order.amount == 0.0 && market_order.side == OrderSide::BUY &&
                         batch_entry.order_type == OrderType::GTC,
                     "order parameter scalars must have safe defaults") &&
               check(history.timestamp == 0 && history.price == 0.0 && activity.id == 0,
                     "market DTO scalars must default to zero") &&
               check(legacy_fee.maker == "maker" && legacy_fee.taker == "taker" && legacy_fee.base_fee.empty(),
                     "fee-rate aggregate field order must remain source compatible") &&
               check(position.size == 0.0 && position.avg_price == 0.0 && position.initial_value == 0.0 &&
                         position.current_value == 0.0 && position.cash_pnl == 0.0 && position.percent_pnl == 0.0 &&
                         position.cur_price == 0.0 && !position.redeemable && !position.mergeable &&
                         position.outcome_index == 0 && !position.negative_risk,
                     "position scalars must have safe defaults");
    }
}
