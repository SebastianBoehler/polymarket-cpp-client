#include "check_support.hpp"
#include "polymarket/environment.hpp"
#include <stdexcept>
#include <string>

namespace
{
    using check_support::check;
    using check_support::expect_equal;
    using polymarket::Environment;

    void expect_invalid(const std::string &name, const std::function<void()> &action)
    {
        check_support::expect_throws<std::invalid_argument>(name, action);
    }

    // Values from Polymarket/ts-sdk packages/client/src/environments.ts and
    // Polymarket/py-sdk src/polymarket/environments.py.
    void test_production_preset()
    {
        const auto env = Environment::production();
        expect_equal("production name", env.name, "production");
        expect_equal("production rpc", env.rpc_url, "https://polygon.drpc.org");
        check(env.contracts.chain_id == 137, "production chain_id must be 137");
        expect_equal("production exchange", env.contracts.standard_exchange,
                     "0xE111180000d2663C0091e4f400237545B87B996B");
        expect_equal("production clob", env.clob_url, "https://clob.polymarket.com");
        expect_equal("production market ws", env.clob_market_ws_url,
                     "wss://ws-subscriptions-clob.polymarket.com/ws/market");
        expect_equal("production user ws", env.clob_user_ws_url,
                     "wss://ws-subscriptions-clob.polymarket.com/ws/user");
        expect_equal("production gamma", env.gamma_url, "https://gamma-api.polymarket.com");
        expect_equal("production data", env.data_url, "https://data-api.polymarket.com");
        expect_equal("production relayer", env.relayer_url, "https://relayer-v2.polymarket.com");
        expect_equal("production rtds", env.rtds_ws_url, "wss://ws-live-data.polymarket.com");
        expect_equal("production sports", env.sports_ws_url, "wss://sports-api.polymarket.com/ws");
        check(env.relayer_max_polls == 100, "production relayer_max_polls must be 100");
        check(env.relayer_poll_interval_ms == 2000,
              "production relayer_poll_interval_ms must be 2000");
        env.validate();
    }

    // Preproduction forks production and changes only four REST hosts.
    void test_preproduction_preset()
    {
        const auto prod = Environment::production();
        const auto env = Environment::preproduction();
        expect_equal("preproduction name", env.name, "preproduction");
        expect_equal("preproduction clob", env.clob_url,
                     "https://clob-preprod-int-v2.polymarket.com");
        expect_equal("preproduction gamma", env.gamma_url,
                     "https://gamma-api-preprod-int.polymarket.com");
        expect_equal("preproduction data", env.data_url,
                     "https://data-api-preprod-int.polymarket.com");
        expect_equal("preproduction relayer", env.relayer_url,
                     "https://relayer-v2-preprod-int.polymarket.com");

        auto restored = env;
        restored.name = prod.name;
        restored.clob_url = prod.clob_url;
        restored.gamma_url = prod.gamma_url;
        restored.data_url = prod.data_url;
        restored.relayer_url = prod.relayer_url;
        check(restored.rpc_url == prod.rpc_url &&
                  restored.contracts.chain_id == prod.contracts.chain_id &&
                  restored.contracts.standard_exchange == prod.contracts.standard_exchange &&
                  restored.contracts.neg_risk_exchange == prod.contracts.neg_risk_exchange &&
                  restored.contracts.collateral_token == prod.contracts.collateral_token &&
                  restored.clob_market_ws_url == prod.clob_market_ws_url &&
                  restored.clob_user_ws_url == prod.clob_user_ws_url &&
                  restored.rtds_ws_url == prod.rtds_ws_url &&
                  restored.sports_ws_url == prod.sports_ws_url &&
                  restored.relayer_max_polls == prod.relayer_max_polls &&
                  restored.relayer_poll_interval_ms == prod.relayer_poll_interval_ms,
              "preproduction must match production outside the four REST hosts");
        env.validate();
    }

    void test_from_name()
    {
        expect_equal("from_name production", Environment::from_name("production").clob_url,
                     Environment::production().clob_url);
        expect_equal("from_name preproduction", Environment::from_name("preproduction").clob_url,
                     Environment::preproduction().clob_url);
        expect_invalid("from_name empty", [] { Environment::from_name(""); });
        expect_invalid("from_name alias", [] { Environment::from_name("preprod"); });
        expect_invalid("from_name case", [] { Environment::from_name("Production"); });
    }

    void test_local_override_is_valid()
    {
        auto env = Environment::production();
        env.name = "local";
        env.clob_url = "http://127.0.0.1:8080";
        env.clob_market_ws_url = "ws://127.0.0.1:8081/ws/market";
        env.relayer_poll_interval_ms = 0;
        env.validate();
    }

    void test_validate_rejects_bad_fields()
    {
        const auto mutated = [](auto mutate)
        {
            return [mutate]
            {
                auto env = Environment::production();
                mutate(env);
                env.validate();
            };
        };
        expect_invalid("empty name", mutated([](Environment &e) { e.name.clear(); }));
        expect_invalid("empty clob", mutated([](Environment &e) { e.clob_url.clear(); }));
        expect_invalid("scheme only", mutated([](Environment &e) { e.gamma_url = "https://"; }));
        expect_invalid("ws scheme for rest",
                       mutated([](Environment &e) { e.data_url = "wss://x"; }));
        expect_invalid("http scheme for ws",
                       mutated([](Environment &e) { e.clob_user_ws_url = "https://x"; }));
        expect_invalid("missing scheme",
                       mutated([](Environment &e) { e.rpc_url = "polygon.drpc.org"; }));
        expect_invalid("zero max polls", mutated([](Environment &e) { e.relayer_max_polls = 0; }));
        expect_invalid("negative poll interval",
                       mutated([](Environment &e) { e.relayer_poll_interval_ms = -1; }));
        expect_invalid("bad contract",
                       mutated([](Environment &e) { e.contracts.standard_exchange = "0x12"; }));
        expect_invalid("zero chain", mutated([](Environment &e) { e.contracts.chain_id = 0; }));
    }
} // namespace

int main()
{
    test_production_preset();
    test_preproduction_preset();
    test_from_name();
    test_local_override_is_valid();
    test_validate_rejects_bad_fields();
    return check_support::finish("test_environment");
}
