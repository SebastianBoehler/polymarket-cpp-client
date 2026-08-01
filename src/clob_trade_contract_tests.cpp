#include "clob_client_test_fixture.hpp"

#include <nlohmann/json.hpp>

namespace clob_test
{
    namespace
    {
        using json = nlohmann::json;

        json maker_order()
        {
            return {
                {"order_id", "maker-1"},
                {"owner", "maker-key"},
                {"maker_address", "0x4444444444444444444444444444444444444444"},
                {"matched_amount", "5.0"},
                {"price", "0.42"},
                {"fee_rate_bps", "5"},
                {"asset_id", "123456789012345678901234567890"},
                {"outcome", "YES"},
                {"side", "SELL"},
            };
        }

        json trade()
        {
            return {
                {"id", "trade-1"},
                {"taker_order_id", "taker-1"},
                {"market", "0x000000000000000000000000000000000000000000000000000000006d61726b"},
                {"asset_id", "123456789012345678901234567890"},
                {"side", "BUY"},
                {"size", "12.5"},
                {"fee_rate_bps", "5"},
                {"price", "0.42"},
                {"status", "MATCHED"},
                {"match_time", "1705322096"},
                {"last_update", "1705322130"},
                {"outcome", "YES"},
                {"bucket_index", 2},
                {"owner", "owner-key"},
                {"maker_address", "0x2222222222222222222222222222222222222222"},
                {"maker_orders", json::array({maker_order()})},
                {"transaction_hash", "0xabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcd"},
                {"trader_side", "TAKER"},
                {"error_msg", "execution failed"},
            };
        }

        json page(json data, const std::string &cursor)
        {
            return {{"data", std::move(data)}, {"next_cursor", cursor}};
        }

        bool test_trade_v2_shape()
        {
            LocalServer server;
            auto client = authenticated_client(server.url());
            server.enqueue(page(json::array({trade()}), "LTE=").dump());
            const auto trades = client.get_trades();

            return check(trades.size() == 1, "Trade V2 row must parse") &&
                   check(trades[0].taker_order_id == "taker-1" &&
                             trades[0].last_update == "1705322130" &&
                             trades[0].outcome == "YES" && trades[0].bucket_index == 2,
                         "Trade V2 scalar fields must be preserved") &&
                   check(trades[0].owner == "owner-key" &&
                             trades[0].maker_address ==
                                 "0x2222222222222222222222222222222222222222" &&
                             trades[0].trader_side == "TAKER" && trades[0].error_msg &&
                             *trades[0].error_msg == "execution failed",
                         "Trade V2 identity and optional fields must be preserved") &&
                   check(trades[0].maker_orders.size() == 1 &&
                             trades[0].maker_orders[0].order_id == "maker-1" &&
                             trades[0].maker_orders[0].matched_amount == "5.0" &&
                             trades[0].maker_orders[0].side == "SELL",
                         "Trade V2 maker orders must be preserved");
        }

        bool test_trade_server_defaults()
        {
            LocalServer server;
            auto client = authenticated_client(server.url());
            auto item = trade();
            item.erase("maker_orders");
            item.erase("transaction_hash");
            item.erase("error_msg");
            auto null_item = trade();
            null_item["maker_orders"] = nullptr;
            null_item["transaction_hash"] = "";
            null_item["error_msg"] = nullptr;
            server.enqueue(
                page(json::array({std::move(item), std::move(null_item)}), "LTE=").dump());
            const auto trades = client.get_trades();

            return check(trades.size() == 2,
                         "server-defaulted Trade V2 rows must parse") &&
                   check(trades[0].maker_orders.empty() && trades[1].maker_orders.empty() &&
                             trades[0].transaction_hash.empty() &&
                             trades[1].transaction_hash.empty() && !trades[0].error_msg &&
                             !trades[1].error_msg,
                         "Trade V2 server defaults must remain empty");
        }

        bool test_trade_page_is_atomic()
        {
            LocalServer server;
            auto client = authenticated_client(server.url());
            auto invalid = trade();
            invalid.erase("last_update");
            server.enqueue(page(json::array({trade(), std::move(invalid)}), "LTE=").dump());
            const auto trades = client.get_trades();

            return check(trades.empty(),
                         "one malformed Trade V2 row must reject the entire page");
        }

        bool test_trade_nested_schema_is_strict()
        {
            LocalServer server;
            auto client = authenticated_client(server.url());
            auto invalid = trade();
            invalid["maker_orders"][0].erase("side");
            server.enqueue(page(json::array({trade(), std::move(invalid)}), "LTE=").dump());
            const auto malformed_maker_orders = client.get_trades();

            LocalServer bucket_server;
            auto bucket_client = authenticated_client(bucket_server.url());
            invalid = trade();
            invalid["bucket_index"] = "2";
            bucket_server.enqueue(page(json::array({std::move(invalid)}), "LTE=").dump());
            const auto malformed_bucket = bucket_client.get_trades();

            return check(malformed_maker_orders.empty(),
                         "malformed maker orders must reject the entire trade page") &&
                   check(malformed_bucket.empty(),
                         "bucket_index must be an unsigned JSON integer");
        }

        bool test_trade_all_pages_are_atomic()
        {
            LocalServer server;
            auto client = authenticated_client(server.url());
            server.enqueue(page(json::array({trade()}), "next").dump());
            server.enqueue(R"({"data":[]})");
            const auto trades = client.get_trades();

            return check(trades.empty(),
                         "a later malformed trade page must discard earlier pages") &&
                   check(server.requests().size() == 2,
                         "trade pagination must reach the malformed second page");
        }

        bool test_trade_page_data_type_is_strict()
        {
            LocalServer server;
            auto client = authenticated_client(server.url());
            server.enqueue(R"({"data":{},"next_cursor":"LTE="})");
            const auto trades = client.get_trades();
            return check(trades.empty(), "trade page data must be an array");
        }
    }

    bool test_trade_v2_contracts()
    {
        return test_trade_v2_shape() && test_trade_server_defaults() &&
               test_trade_page_is_atomic() && test_trade_nested_schema_is_strict() &&
               test_trade_all_pages_are_atomic() && test_trade_page_data_type_is_strict();
    }
}

int main()
{
    polymarket::http_global_init();
    const bool ok = clob_test::test_trade_v2_contracts();
    polymarket::http_global_cleanup();
    return ok ? 0 : 1;
}
