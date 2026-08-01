#include "clob_client_test_fixture.hpp"

namespace clob_test
{
    bool test_order_post_response_contracts()
    {
        LocalServer server;
        auto client = authenticated_client(server.url());
        SignedOrder order;
        order.salt = "1";

        server.enqueue(R"({"success":true})");
        const auto missing_identity = client.post_order_result(order, OrderType::GTC);
        server.enqueue(R"({"success":true,"orderID":"order-1","status":"live","transactionsHashes":[7]})");
        const auto malformed_hashes = client.post_order_result(order, OrderType::GTC);
        server.enqueue(R"({"success":true,"orderID":"order-1","status":"live","makingAmount":"1","takingAmount":"2","transactionsHashes":["0x1"],"tradeIDs":["trade-1"]})");
        const auto accepted = client.post_order_result(order, OrderType::GTC);
        server.enqueue(R"({"success":true,"orderID":"order-2","status":"matched","transactionHashes":["0x2"],"tradeIds":["trade-2"]})");
        const auto camel_accepted = client.post_order_result(order, OrderType::GTC);
        server.enqueue(R"({"success":true,"orderID":"order-3","status":"live","transactionsHashes":null,"tradeIDs":null})");
        const auto null_defaults = client.post_order_result(order, OrderType::GTC);
        server.enqueue(R"({"success":true,"orderID":"order-4","status":"live","tradeIDs":[7]})");
        const auto malformed_trade_ids = client.post_order_result(order, OrderType::GTC);

        server.enqueue(R"({"error":"unavailable"})", 500);
        const auto transport_batch = client.post_orders({{order, OrderType::GTC}});
        server.enqueue(R"({"success":true})");
        const auto malformed_batch = client.post_orders({{order, OrderType::GTC}});
        server.enqueue(R"([{"success":true}])");
        const auto malformed_batch_item = client.post_orders({{order, OrderType::GTC}});
        server.enqueue(R"([{"success":true,"orderID":"batch-1","status":"live"}])");
        const auto accepted_batch = client.post_orders({{order, OrderType::GTC}});

        const auto parse_failure = [](const auto &result)
        {
            return !result && result.error().code == SdkErrorCode::Parse;
        };
        const auto explicit_batch_failure = [](const auto &responses)
        {
            return !responses.empty() && !responses.front().success &&
                   !responses.front().error_msg.empty();
        };
        return check(parse_failure(missing_identity),
                     "successful order Result must require order identity and status") &&
               check(parse_failure(malformed_hashes),
                     "order Result must reject malformed transaction hashes") &&
               check(accepted && accepted.value().order_id == "order-1" &&
                         accepted.value().transaction_hashes ==
                             std::vector<std::string>{"0x1"} &&
                         accepted.value().trade_ids ==
                             std::vector<std::string>{"trade-1"},
                     "order response must retain transaction and trade IDs") &&
               check(camel_accepted && camel_accepted.value().transaction_hashes ==
                                           std::vector<std::string>{"0x2"} &&
                         camel_accepted.value().trade_ids ==
                             std::vector<std::string>{"trade-2"},
                     "order response must accept compatible camel aliases") &&
               check(null_defaults && null_defaults.value().transaction_hashes.empty() &&
                         null_defaults.value().trade_ids.empty(),
                     "null order response ID collections must default to empty") &&
               check(parse_failure(malformed_trade_ids),
                     "order Result must reject malformed trade IDs") &&
               check(explicit_batch_failure(transport_batch),
                     "batch post must expose HTTP failure explicitly") &&
               check(explicit_batch_failure(malformed_batch),
                     "batch post must reject a malformed top-level response") &&
               check(explicit_batch_failure(malformed_batch_item),
                     "batch post must reject malformed response items") &&
               check(accepted_batch.size() == 1 && accepted_batch[0].success &&
                         accepted_batch[0].order_id == "batch-1",
                     "valid strict batch response must remain accepted");
    }

    bool test_cancellation_response_contracts()
    {
        LocalServer server;
        auto client = authenticated_client(server.url());

        server.enqueue(R"({"canceled":[],"not_canceled":{"order-1":"still live"}})");
        const auto rejected = client.cancel_order_result("order-1");
        server.enqueue(R"({"canceled":[],"notCanceled":{"order-1":"still live"}})");
        const auto camel_rejected = client.cancel_order_result("order-1");
        server.enqueue(R"({"canceled":"order-1","not_canceled":{}})");
        const auto malformed = client.cancel_order_result("order-1");
        server.enqueue(R"({"canceled":["order-1"]})");
        const auto missing_not_canceled = client.cancel_order_result("order-1");
        server.enqueue(R"({"canceled":["order-1"],"notCanceled":null})");
        const auto null_not_canceled = client.cancel_order_result("order-1");
        server.enqueue(R"({"notCanceled":{"order-1":"still live"}})");
        const auto missing_canceled = client.cancel_order_result("order-1");
        server.enqueue(R"({"canceled":null,"notCanceled":{"order-1":"still live"}})");
        const auto null_canceled = client.cancel_order_result("order-1");

        server.enqueue(R"({"canceled":["order-1"],"not_canceled":{"order-2":"still live"}})");
        const bool partial_batch = client.cancel_orders({"order-1", "order-2"});
        server.enqueue(R"({"canceled":[],"not_canceled":{"order-3":"still live"}})");
        const bool partial_all = client.cancel_all();
        server.enqueue(R"({"canceled":[],"not_canceled":{"order-4":"still live"}})");
        const bool partial_market = client.cancel_market_orders("market-1");

        const auto api_failure = [](const auto &result)
        {
            return !result && result.error().code == SdkErrorCode::ApiResponse;
        };
        return check(api_failure(rejected) && api_failure(camel_rejected),
                     "snake and camel cancellation failures must be API failures") &&
               check(!malformed && malformed.error().code == SdkErrorCode::Parse,
                     "malformed cancellation Result must be a parse failure") &&
               check(missing_not_canceled && null_not_canceled,
                     "missing or null notCanceled must default to empty") &&
               check(api_failure(missing_canceled) && api_failure(null_canceled),
                     "missing or null canceled must default to empty") &&
               check(!partial_batch && !partial_all && !partial_market,
                     "legacy cancellation methods must reject partial cancellations");
    }
}
