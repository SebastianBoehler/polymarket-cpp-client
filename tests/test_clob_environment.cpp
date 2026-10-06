#include "../src/clob_client_test_fixture.hpp"

namespace clob_test
{
    namespace
    {
        constexpr const char *private_key =
            "0x0000000000000000000000000000000000000000000000000000000000000001";
        constexpr const char *wallet = "0x7E5F4552091A69125d5DfCb7b8C2659029395Bdf";

        Environment local_environment(const LocalServer &clob, const LocalServer &data)
        {
            auto environment = Environment::production();
            environment.name = "local";
            environment.clob_url = clob.url();
            environment.data_url = data.url();
            environment.contracts.standard_exchange = "0x1111111111111111111111111111111111111111";
            environment.contracts.neg_risk_exchange = "0x2222222222222222222222222222222222222222";
            return environment;
        }

        template <typename Construct> bool rejects(Construct construct)
        {
            try
            {
                construct();
            }
            catch (const std::invalid_argument &)
            {
                return true;
            }
            catch (...)
            {
            }
            return false;
        }
    } // namespace

    bool test_clob_environment_routes_hosts_and_contracts()
    {
        LocalServer clob;
        LocalServer data;
        const auto environment = local_environment(clob, data);
        ClobClient client(environment, private_key, ApiCredentials{"key", "c2VjcmV0", "pass"});
        clob.enqueue("1722510000");
        const bool warmed = client.warm_connection();
        data.enqueue("[]");
        const auto positions = client.get_positions(wallet);
        const auto clob_requests = clob.requests();
        const auto data_requests = data.requests();

        return check(warmed && clob_requests.size() == 1 && clob_requests[0].target == "/time",
                     "CLOB requests must use Environment.clob_url") &&
               check(positions.empty() && data_requests.size() == 1 &&
                         data_requests[0].target.rfind("/positions?", 0) == 0,
                     "Data API requests must use Environment.data_url") &&
               check(client.get_exchange_address() == environment.contracts.standard_exchange &&
                         client.get_neg_risk_exchange_address() ==
                             environment.contracts.neg_risk_exchange,
                     "order exchanges must come from Environment.contracts") &&
               check(client.environment().name == "local" && client.get_address() == wallet,
                     "client must keep the environment and signer");
    }

    bool test_clob_legacy_constructors_use_production_environment()
    {
        LocalServer server;
        ClobClient client(server.url(), 137);
        const auto production = Environment::production();
        return check(client.environment().clob_url == server.url() &&
                         client.environment().data_url == production.data_url &&
                         client.get_exchange_address() == production.contracts.standard_exchange &&
                         client.get_neg_risk_exchange_address() ==
                             production.contracts.neg_risk_exchange,
                     "legacy constructors must override only the CLOB host");
    }

    bool test_clob_environment_constructors_validate()
    {
        auto bad_url = Environment::production();
        bad_url.data_url = "data-api.polymarket.com";
        auto bad_contract = Environment::production();
        bad_contract.contracts.standard_exchange = "0x12";
        auto wide_chain = Environment::production();
        wide_chain.contracts.chain_id = 1ULL << 40;
        ApiCredentials credentials{"key", "c2VjcmV0", "pass"};

        return check(rejects([&] { ClobClient client(bad_url); }) &&
                         rejects([&] { ClobClient client(bad_contract, private_key); }) &&
                         rejects([&] { ClobClient client(wide_chain, private_key, credentials); }),
                     "Environment constructors must reject invalid environments") &&
               check(rejects(
                         [&]
                         {
                             ClobClient client(Environment::production(), private_key,
                                               SignatureType::POLY_GNOSIS_SAFE);
                         }),
                     "Environment constructors must still require a non-EOA funder");
    }

    // Legacy constructors sign against the production contracts, so they accept
    // only chain 137; Amoy (80002) has no contract preset.
    bool test_clob_constructors_reject_unsupported_chains()
    {
        constexpr const char *funder = "0x1111111111111111111111111111111111111111";
        LocalServer server;
        HttpClientOptions options;
        ApiCredentials credentials{"key", "c2VjcmV0", "pass"};

        const bool all_rejected =
            rejects([&] { ClobClient client(server.url(), 1); }) &&
            rejects([&] { ClobClient client(server.url(), 80002); }) &&
            rejects([&] { ClobClient client(server.url(), 1, options); }) &&
            rejects([&] { ClobClient client(server.url(), 1, private_key); }) &&
            rejects(
                [&]
                {
                    ClobClient client(server.url(), 1, private_key, SignatureType::POLY_PROXY,
                                      funder, options);
                }) &&
            rejects([&] { ClobClient client(server.url(), 1, private_key, credentials); }) &&
            rejects(
                [&]
                {
                    ClobClient client(server.url(), 1, private_key, credentials,
                                      SignatureType::POLY_PROXY, funder, options);
                });
        ClobClient polygon(server.url(), 137);
        return check(all_rejected, "every CLOB constructor must reject unsupported chains") &&
               check(polygon.get_address().empty(), "Polygon mainnet must remain supported");
    }
} // namespace clob_test

int main()
{
    using namespace clob_test;
    polymarket::http_global_init();
    const bool ok = test_clob_environment_routes_hosts_and_contracts() &&
                    test_clob_legacy_constructors_use_production_environment() &&
                    test_clob_environment_constructors_validate() &&
                    test_clob_constructors_reject_unsupported_chains();
    polymarket::http_global_cleanup();
    return ok ? 0 : 1;
}
