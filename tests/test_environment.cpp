#include "check_support.hpp"
#include "polymarket/environment.hpp"
#include "polymarket/position_client.hpp"
#include "polymarket/types.hpp"
#include <stdexcept>
#include <string>

namespace
{
    using check_support::check;
    using check_support::expect_equal;
    using polymarket::Config;
    using polymarket::Environment;
    using polymarket::PositionClientConfig;

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
                  restored.rtds_ws_url == prod.rtds_ws_url,
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
        expect_invalid("bad contract",
                       mutated([](Environment &e) { e.contracts.standard_exchange = "0x12"; }));
        expect_invalid("zero chain", mutated([](Environment &e) { e.contracts.chain_id = 0; }));
    }

    // Config and PositionClientConfig repeat the production URLs as member
    // defaults; keep them in step with the production preset.
    void test_default_configs_match_production()
    {
        const auto production = Environment::production();
        const Config config;
        const auto from_preset = Config::for_environment(production);
        expect_equal("default clob rest", config.clob_rest_url, from_preset.clob_rest_url);
        expect_equal("default market ws", config.clob_ws_url, from_preset.clob_ws_url);
        expect_equal("default user ws", config.clob_user_ws_url, from_preset.clob_user_ws_url);
        expect_equal("default gamma", config.gamma_api_url, from_preset.gamma_api_url);
        expect_equal("default rtds", config.rtds_ws_url, from_preset.rtds_ws_url);

        const PositionClientConfig position;
        expect_equal("default position gamma", position.gamma_api_url, production.gamma_url);
        expect_equal("default position relayer", position.relayer_url, production.relayer_url);
        check(position.contracts.chain_id == production.contracts.chain_id &&
                  position.contracts.standard_exchange == production.contracts.standard_exchange &&
                  position.contracts.collateral_token == production.contracts.collateral_token,
              "default PositionClientConfig contracts must match production");
        check(position.rpc_url.empty(), "default PositionClientConfig must not pick an RPC");
    }

    void test_config_for_environment()
    {
        auto env = Environment::preproduction();
        env.clob_market_ws_url = "ws://127.0.0.1:9001/ws/market";
        env.clob_user_ws_url = "ws://127.0.0.1:9001/ws/user";
        env.rtds_ws_url = "ws://127.0.0.1:9002";
        const auto config = Config::for_environment(env);
        const Config defaults;
        expect_equal("config clob rest", config.clob_rest_url, env.clob_url);
        expect_equal("config market ws", config.clob_ws_url, env.clob_market_ws_url);
        expect_equal("config user ws", config.clob_user_ws_url, env.clob_user_ws_url);
        expect_equal("config gamma", config.gamma_api_url, env.gamma_url);
        expect_equal("config rtds", config.rtds_ws_url, env.rtds_ws_url);
        check(config.http_timeout_ms == defaults.http_timeout_ms &&
                  config.ws_ping_interval_ms == defaults.ws_ping_interval_ms &&
                  config.max_markets == defaults.max_markets &&
                  config.crypto_tickers == defaults.crypto_tickers,
              "Config::for_environment must keep non-endpoint defaults");

        env.gamma_url.clear();
        expect_invalid("config invalid environment", [&] { (void)Config::for_environment(env); });
    }

    void test_position_config_for_environment()
    {
        auto env = Environment::preproduction();
        env.rpc_url = "http://127.0.0.1:8545";
        env.contracts.standard_exchange = "0x1111111111111111111111111111111111111111";
        const auto config = PositionClientConfig::for_environment(env);
        const PositionClientConfig defaults;
        expect_equal("position rpc", config.rpc_url, env.rpc_url);
        expect_equal("position gamma", config.gamma_api_url, env.gamma_url);
        expect_equal("position relayer", config.relayer_url, env.relayer_url);
        expect_equal("position exchange", config.contracts.standard_exchange,
                     env.contracts.standard_exchange);
        check(config.contracts.chain_id == env.contracts.chain_id,
              "PositionClientConfig::for_environment must copy the chain");
        check(config.private_key.empty() && config.funder_address.empty() &&
                  config.wallet_type == defaults.wallet_type && config.relayer_api_key.empty() &&
                  config.rpc_timeout_ms == defaults.rpc_timeout_ms &&
                  config.relayer_retry_delay_ms == defaults.relayer_retry_delay_ms &&
                  config.relayer_max_submit_retries == defaults.relayer_max_submit_retries,
              "PositionClientConfig::for_environment must leave wallet and retry settings alone");

        env.contracts.collateral_token = "0x12";
        expect_invalid("position invalid environment",
                       [&] { (void)PositionClientConfig::for_environment(env); });
    }
} // namespace

int main()
{
    test_production_preset();
    test_preproduction_preset();
    test_from_name();
    test_local_override_is_valid();
    test_validate_rejects_bad_fields();
    test_default_configs_match_production();
    test_config_for_environment();
    test_position_config_for_environment();
    return check_support::finish("test_environment");
}
