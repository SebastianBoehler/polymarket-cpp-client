#include "polymarket/environment.hpp"
#include "polymarket/position_client.hpp"
#include "polymarket/types.hpp"
#include <stdexcept>
#include <utility>
#include <vector>

namespace polymarket
{
    namespace
    {
        bool has_scheme_and_host(const std::string &url, std::string_view scheme)
        {
            return url.size() > scheme.size() && url.compare(0, scheme.size(), scheme) == 0;
        }

        void require_url(const char *field, const std::string &url, std::string_view secure_scheme,
                         std::string_view plain_scheme)
        {
            if (has_scheme_and_host(url, secure_scheme) || has_scheme_and_host(url, plain_scheme))
                return;
            throw std::invalid_argument(std::string("Environment.") + field + " must be a " +
                                        std::string(secure_scheme) + " or " +
                                        std::string(plain_scheme) + " URL");
        }
    } // namespace

    Environment Environment::production()
    {
        Environment env;
        env.name = "production";
        env.rpc_url = "https://polygon.drpc.org";
        env.contracts = PolymarketContracts::polygon_mainnet();
        env.clob_url = "https://clob.polymarket.com";
        env.clob_market_ws_url = "wss://ws-subscriptions-clob.polymarket.com/ws/market";
        env.clob_user_ws_url = "wss://ws-subscriptions-clob.polymarket.com/ws/user";
        env.gamma_url = "https://gamma-api.polymarket.com";
        env.data_url = "https://data-api.polymarket.com";
        env.relayer_url = "https://relayer-v2.polymarket.com";
        env.rtds_ws_url = "wss://ws-live-data.polymarket.com";
        env.sports_ws_url = "wss://sports-api.polymarket.com/ws";
        return env;
    }

    Environment Environment::preproduction()
    {
        Environment env = production();
        env.name = "preproduction";
        env.clob_url = "https://clob-preprod-int-v2.polymarket.com";
        env.gamma_url = "https://gamma-api-preprod-int.polymarket.com";
        env.data_url = "https://data-api-preprod-int.polymarket.com";
        env.relayer_url = "https://relayer-v2-preprod-int.polymarket.com";
        return env;
    }

    Environment Environment::from_name(std::string_view name)
    {
        if (name == "production") return production();
        if (name == "preproduction") return preproduction();
        throw std::invalid_argument("unknown Polymarket environment \"" + std::string(name) +
                                    "\"; expected production or preproduction");
    }

    void Environment::validate() const
    {
        if (name.empty()) throw std::invalid_argument("Environment.name must be non-empty");

        const std::vector<std::pair<const char *, const std::string *>> rest_urls = {
            {"rpc_url", &rpc_url},   {"clob_url", &clob_url},       {"gamma_url", &gamma_url},
            {"data_url", &data_url}, {"relayer_url", &relayer_url},
        };
        for (const auto &[field, url] : rest_urls)
            require_url(field, *url, "https://", "http://");

        const std::vector<std::pair<const char *, const std::string *>> ws_urls = {
            {"clob_market_ws_url", &clob_market_ws_url},
            {"clob_user_ws_url", &clob_user_ws_url},
            {"rtds_ws_url", &rtds_ws_url},
            {"sports_ws_url", &sports_ws_url},
        };
        for (const auto &[field, url] : ws_urls)
            require_url(field, *url, "wss://", "ws://");

        if (relayer_max_polls <= 0)
            throw std::invalid_argument("Environment.relayer_max_polls must be positive");
        if (relayer_poll_interval_ms < 0)
            throw std::invalid_argument(
                "Environment.relayer_poll_interval_ms must be non-negative");

        contracts.validate();
    }

    Config Config::for_environment(const Environment &environment)
    {
        environment.validate();
        Config config;
        config.clob_rest_url = environment.clob_url;
        config.clob_ws_url = environment.clob_market_ws_url;
        config.clob_user_ws_url = environment.clob_user_ws_url;
        config.gamma_api_url = environment.gamma_url;
        config.rtds_ws_url = environment.rtds_ws_url;
        return config;
    }

    PositionClientConfig PositionClientConfig::for_environment(const Environment &environment)
    {
        environment.validate();
        PositionClientConfig config;
        config.rpc_url = environment.rpc_url;
        config.gamma_api_url = environment.gamma_url;
        config.relayer_url = environment.relayer_url;
        config.contracts = environment.contracts;
        return config;
    }

} // namespace polymarket
