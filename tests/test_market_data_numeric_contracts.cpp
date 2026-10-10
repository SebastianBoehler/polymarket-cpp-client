#include "polymarket/clob_client.hpp"
#include "clob_client_test_fixture.hpp"
#include "polymarket/market_fetcher.hpp"
#include "rest_numeric.hpp"

#include <clocale>
#include <cmath>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
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
        R"({"asset_id":"A","bids":[{"price":"0.4","size":-1}],"asks":[]})",
        R"({"asset_id":"A","bids":[{"price":"0.4","size":"1"},{"price":"0.40","size":"2"}],"asks":[]})",
        R"({"asset_id":"A","bids":[],"asks":[{"price":0.6,"size":"1"},{"price":"0.5","size":"1"},{"price":"0.60","size":"1"}]})"};

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
        server.enqueue(R"({"minimum_tick_size":"0.01"})");
        server.enqueue(R"({"neg_risk":false})");
        (void)client.get_tick_size("A");
        (void)client.get_neg_risk("A");
        constexpr const char *seeding_a =
            R"({"asset_id":"A","tick_size":"0.001","neg_risk":true,"bids":[],"asks":[]})";
        for (const std::string &body :
             {std::string("[") + seeding_a + "]",
              std::string("[") + seeding_a + "," + seeding_a + "]",
              std::string("[") + seeding_a + R"(,{"asset_id":"C","bids":[],"asks":[]}])",
              std::string("[") + seeding_a + R"(,{"asset_id":"B","bids":[]}])"})
        {
            server.enqueue(body);
            if (!check(client.get_order_books({"A", "B"}).empty(),
                       "batch books reject missing, duplicate, unexpected, or malformed identities"))
            {
                return false;
            }
        }
        const auto requests_before = server.requests().size();
        const auto tick = client.get_tick_size("A");
        const auto neg_risk = client.get_neg_risk("A");
        return check(tick && tick->minimum_tick_size == "0.01" && neg_risk && !neg_risk->neg_risk &&
                         server.requests().size() == requests_before,
                     "rejected batch books must leave the tick and neg-risk caches unchanged");
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

namespace
{
    std::optional<double> strict_number(const std::string &text)
    {
        try
        {
            return detail::strict_json_number(nlohmann::json(text));
        }
        catch (const std::invalid_argument &)
        {
            return std::nullopt;
        }
    }

    bool test_numeric_strings_use_the_decimal_grammar()
    {
        const std::vector<std::pair<std::string, double>> accepted = {
            {"0.5", 0.5},
            {"0.001", 0.001},
            {"0", 0.0},
            {"1", 1.0},
            {"100", 100.0},
            {"123.25", 123.25},
            {"1e-3", 0.001},
            {"1E2", 100.0},
            {"2.5e+1", 25.0},
            {".5", 0.5},
            {"5.", 5.0},
            {"+0.5", 0.5},
            {"+.5", 0.5},
            {"-0.5", -0.5},
            {"-.5", -0.5},
            {"00.10", 0.1},
            {"0.1000000000000000055511151231257827", 0.1}};
        const std::vector<std::string> rejected = {
            "",    " ",   " 0.5", "0.5 ",     "+",     "-",      ".",     "+-1",   "-+1",
            "++1", "1e",  "1e+",  "e5",       "0x10",  "0x1p3",  "1,5",   "1.2.3", "nan",
            "NaN", "inf", "-inf", "infinity", "1e400", "-1e400", "0.5\n", "\t1"};
        bool ok = true;
        for (const auto &[text, value] : accepted)
        {
            if (strict_number(text) != value)
            {
                std::cerr << "decimal string '" << text << "' was not read as " << value << "\n";
                ok = false;
            }
        }
        for (const auto &text : rejected)
        {
            if (strict_number(text))
            {
                std::cerr << "non-decimal string '" << text << "' was accepted\n";
                ok = false;
            }
        }

        for (const auto *name : {"de_DE.UTF-8", "de_DE.utf8", "fr_FR.UTF-8"})
        {
            if (!std::setlocale(LC_NUMERIC, name)) continue;
            const bool comma_locale_reads_dots =
                strict_number("0.25") == 0.25 && !strict_number("0,25");
            std::setlocale(LC_NUMERIC, "C");
            ok &= check(comma_locale_reads_dots,
                        "decimal strings must use '.' regardless of the C locale");
            break;
        }
        return check(ok, "numeric strings must follow the decimal grammar");
    }
} // namespace

int main()
{
    http_global_init();
    const bool ok = test_clob_orderbook_numbers() &&
                    test_clob_batch_orderbook_identity_is_atomic() &&
                    test_market_fetcher_orderbook_numbers() && test_scalar_market_data_numbers() &&
                    test_batch_market_data_is_atomic() && test_tick_size_numbers() &&
                    test_numeric_strings_use_the_decimal_grammar();
    http_global_cleanup();
    return ok ? 0 : 1;
}
