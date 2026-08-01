#include "clob_client_test_fixture.hpp"

#include <string>

namespace clob_test
{
    namespace
    {
        void enqueue_cursor_cycle(LocalServer &server)
        {
            server.enqueue(R"({"next_cursor":"A","data":[]})");
            server.enqueue(R"({"next_cursor":"B","data":[]})");
            server.enqueue(R"({"next_cursor":"A","data":[]})");
        }

        bool check_three_page_cycle(const LocalServer &server, const std::string &label)
        {
            return check(server.requests().size() == 3,
                         label + " must reject A-to-B-to-A before a fourth request");
        }

        bool test_open_orders_cycle_result()
        {
            LocalServer server;
            auto client = authenticated_client(server.url());
            enqueue_cursor_cycle(server);
            const auto result = client.get_open_orders_result();
            return check(!result, "open-orders Result must reject cursor cycles") &&
                   check(result.error().code == SdkErrorCode::Parse,
                         "open-orders cursor cycles must be Parse errors") &&
                   check_three_page_cycle(server, "open orders");
        }

        bool test_open_orders_cycle_legacy()
        {
            LocalServer server;
            auto client = authenticated_client(server.url());
            enqueue_cursor_cycle(server);
            const auto orders = client.get_open_orders();
            return check(orders.empty(), "legacy open-orders must fail closed on cursor cycles") &&
                   check_three_page_cycle(server, "legacy open orders");
        }

        bool test_trades_cycle()
        {
            LocalServer server;
            auto client = authenticated_client(server.url());
            enqueue_cursor_cycle(server);
            const auto trades = client.get_trades();
            return check(trades.empty(), "trades must fail closed on cursor cycles") &&
                   check_three_page_cycle(server, "trades");
        }

        bool test_current_rewards_cycle()
        {
            LocalServer server;
            ClobClient client(server.url(), 137);
            enqueue_cursor_cycle(server);
            const auto rewards = client.get_rewards_markets_current();
            return check(rewards.empty(), "current rewards must fail closed on cursor cycles") &&
                   check_three_page_cycle(server, "current rewards");
        }

        bool test_raw_rewards_cycle()
        {
            LocalServer server;
            ClobClient client(server.url(), 137);
            enqueue_cursor_cycle(server);
            const auto rewards = client.get_rewards_markets("condition");
            return check(rewards.empty(), "raw rewards must fail closed on cursor cycles") &&
                   check_three_page_cycle(server, "raw rewards");
        }

        bool test_earnings_cycle()
        {
            LocalServer server;
            auto client = authenticated_client(server.url());
            enqueue_cursor_cycle(server);
            const auto earnings = client.get_earnings_for_user_for_day_all("2024-01-01");
            return check(earnings.empty(), "earnings must fail closed on cursor cycles") &&
                   check_three_page_cycle(server, "earnings");
        }

        bool test_open_orders_page_limit()
        {
            LocalServer server;
            auto client = authenticated_client(server.url());
            for (size_t page = 1; page <= 1000; ++page)
            {
                server.enqueue("{\"next_cursor\":\"cursor-" +
                               std::to_string(page) + "\",\"data\":[]}");
            }
            const auto result = client.get_open_orders_result();
            return check(!result, "open-orders Result must reject an unterminated 1,000-page response") &&
                   check(result.error().code == SdkErrorCode::Parse,
                         "open-orders page-limit failures must be Parse errors") &&
                   check(server.requests().size() == 1000,
                         "open-orders page limit must prevent request 1,001");
        }
    }

    bool test_pagination_cycle_and_page_limit_guards()
    {
        return test_open_orders_cycle_result() && test_open_orders_cycle_legacy() &&
               test_trades_cycle() && test_current_rewards_cycle() && test_raw_rewards_cycle() &&
               test_earnings_cycle() && test_open_orders_page_limit();
    }
}
