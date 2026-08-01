#include "clob_client_test_fixture.hpp"

namespace clob_test
{
    bool test_credentials_validation()
    {
        constexpr const char *private_key =
            "0x0000000000000000000000000000000000000000000000000000000000000001";
        LocalServer server;
        HttpClientOptions options;
        options.timeout_ms = 1'000;
        ClobClient client(server.url(), 137, private_key, SignatureType::EOA, "", options);
        const std::vector<std::string> malformed = {
            R"({"apiKey":"","secret":"c2VjcmV0","passphrase":"pass"})",
            R"({"apiKey":"key","secret":"","passphrase":"pass"})",
            R"({"apiKey":"key","secret":"c2VjcmV0","passphrase":""})",
            R"({"apiKey":"key","secret":"!","passphrase":"pass"})"};
        std::size_t rejected = 0;
        for (const auto &body : malformed)
        {
            server.enqueue(body);
            try
            {
                (void)client.derive_api_key();
            }
            catch (const std::exception &)
            {
                ++rejected;
            }
        }

        const auto constructor_rejects = [&](const ApiCredentials &credentials)
        {
            try
            {
                ClobClient invalid(server.url(), 137, private_key, credentials);
            }
            catch (const std::invalid_argument &)
            {
                return true;
            }
            return false;
        };
        const bool invalid_provided_rejected =
            constructor_rejects({"", "c2VjcmV0", "pass"}) &&
            constructor_rejects({"key", "", "pass"}) &&
            constructor_rejects({"key", "c2VjcmV0", ""}) &&
            constructor_rejects({"key", "!", "pass"});
        return check(rejected == malformed.size() && !client.is_authenticated(),
                     "malformed derived credentials must not be installed") &&
               check(invalid_provided_rejected,
                     "malformed provided credentials must be rejected eagerly") &&
               check(server.requests().size() == malformed.size(),
                     "credential validation uses only requested derivation calls");
    }
}
