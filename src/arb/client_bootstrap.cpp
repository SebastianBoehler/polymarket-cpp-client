#include "arb/client_bootstrap.hpp"
#include "http_client.hpp"
#include <cstdlib>
#include <stdexcept>
#include <utility>

namespace polymarket::arb
{

    std::optional<ApiCredentials> api_credentials_from_env()
    {
        const char *api_key = std::getenv("API_KEY");
        const char *api_secret = std::getenv("API_SECRET");
        const char *api_passphrase = std::getenv("API_PASSPHRASE");
        if (!api_key || !api_secret || !api_passphrase)
        {
            return std::nullopt;
        }

        ApiCredentials creds;
        creds.api_key = api_key;
        creds.api_secret = api_secret;
        creds.api_passphrase = api_passphrase;
        return creds;
    }

    AuthenticatedSession create_authenticated_session(
        const std::string &base_url,
        int chain_id,
        const std::string &private_key,
        const std::string &funder_address,
        const std::optional<ApiCredentials> &api_credentials)
    {
        OrderSigner signer(private_key, chain_id);
        AuthenticatedSession session;
        session.signer_address = signer.address();
        session.funder_address =
            funder_address.empty() ? session.signer_address : funder_address;
        session.signature_type =
            session.funder_address == session.signer_address
                ? SignatureType::EOA
                : SignatureType::POLY_GNOSIS_SAFE;

        ApiCredentials creds;
        if (api_credentials.has_value())
        {
            creds = *api_credentials;
        }
        else
        {
            HttpClient http;
            http.set_base_url(base_url);
            creds = signer.create_or_derive_api_credentials(http, session.funder_address);
        }

        session.client = std::make_shared<ClobClient>(
            base_url,
            chain_id,
            private_key,
            creds,
            session.signature_type,
            session.funder_address);
        return session;
    }

} // namespace polymarket::arb
