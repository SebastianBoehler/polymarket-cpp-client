#include "polymarket/market_fetcher.hpp"
#include "position_test_support.hpp"
#include "safe_relayer.hpp"

#include <stdexcept>

using namespace position_test;

namespace
{
    const std::map<std::string, std::string> retry_now = {{"Retry-After", "0"}};
    constexpr const char *slow_down = R"({"error":"slow down"})";

    PositionClientConfig eoa_config(const std::string &gamma_url)
    {
        PositionClientConfig config;
        config.private_key = kKey;
        config.rpc_url = "http://127.0.0.1:1";
        config.gamma_api_url = gamma_url;
        return config;
    }

    void gamma_lookup_retries()
    {
        clob_test::LocalServer gamma;
        PositionClient client(eoa_config(gamma.url()));
        gamma.enqueue(slow_down, 429, retry_now);
        gamma.enqueue("[]");
        expect_throws<std::invalid_argument>("empty market lookup",
                                             [&] { client.resolve_market(kCondition); });
        check(gamma.requests().size() == 2, "a rate-limited Gamma lookup must be retried");

        auto disabled_config = eoa_config(gamma.url());
        disabled_config.rate_limit_retry = std::nullopt;
        PositionClient disabled(disabled_config);
        gamma.enqueue(slow_down, 429, retry_now);
        expect_throws<std::runtime_error>("rate-limited lookup",
                                          [&] { disabled.resolve_market(kCondition); });
        check(gamma.requests().size() == 3, "a disabled policy must not retry Gamma");

        auto invalid = eoa_config(gamma.url());
        invalid.rate_limit_retry = RateLimitRetry{-1, std::chrono::milliseconds(0)};
        expect_throws<std::invalid_argument>("invalid PositionClient policy",
                                             [&] { PositionClient bad(invalid); });
    }

    void relayer_reads_retry_but_submit_does_not()
    {
        clob_test::LocalServer server;
        detail::RelayerClient relayer(server.url(), "relayer-key", kWallet);
        const nlohmann::json transaction = {{"transaction_id", "tx-1"},
                                            {"transaction_hash", "0x1"},
                                            {"state", "STATE_MINED"},
                                            {"error_msg", ""}};

        server.enqueue(slow_down, 429, retry_now);
        server.enqueue(transaction.dump());
        check(relayer.get_transaction("tx-1").transaction_id == "tx-1",
              "status read must succeed after a 429");
        server.enqueue(slow_down, 429, retry_now);
        server.enqueue(R"({"nonce":"7"})");
        check(relayer.safe_nonce(kWallet) == "7", "nonce read must succeed after a 429");
        check(server.requests().size() == 4, "relayer reads must retry a 429");

        server.enqueue(slow_down, 429, retry_now);
        expect_throws<detail::RelayerRequestError>("rate-limited submit", [&]
                                                   { relayer.submit(nlohmann::json::object()); });
        check(server.requests().size() == 5, "relayer submits must not be retried here");
    }

    void market_fetcher_retries()
    {
        clob_test::LocalServer server;
        Config config;
        config.clob_rest_url = server.url();
        MarketFetcher fetcher(config);
        server.enqueue(slow_down, 429, retry_now);
        server.enqueue(R"({"asset_id":"token-1","bids":[],"asks":[]})");
        const auto book = fetcher.fetch_orderbook("token-1");
        check(book && server.requests().size() == 2, "MarketFetcher reads must retry a 429");

        config.rate_limit_retry = std::nullopt;
        MarketFetcher disabled(config);
        server.enqueue(slow_down, 429, retry_now);
        check(!disabled.fetch_orderbook("token-1") && server.requests().size() == 3,
              "a disabled MarketFetcher policy must not retry");

        config.rate_limit_retry = RateLimitRetry{1, std::chrono::milliseconds(-1)};
        expect_throws<std::invalid_argument>("invalid MarketFetcher policy",
                                             [&] { MarketFetcher bad(config); });
    }
} // namespace

int main()
{
    http_global_init();
    gamma_lookup_retries();
    relayer_reads_retry_but_submit_does_not();
    market_fetcher_retries();
    http_global_cleanup();
    return check_support::finish("test_position_rate_limit");
}
