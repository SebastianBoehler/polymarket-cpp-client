#pragma once

#include "clob_client.hpp"
#include "order_signer.hpp"
#include <memory>
#include <optional>
#include <string>

namespace polymarket::arb
{

    struct AuthenticatedSession
    {
        std::shared_ptr<ClobClient> client;
        std::string signer_address;
        std::string funder_address;
        SignatureType signature_type = SignatureType::EOA;
    };

    std::optional<ApiCredentials> api_credentials_from_env();

    AuthenticatedSession create_authenticated_session(
        const std::string &base_url,
        int chain_id,
        const std::string &private_key,
        const std::string &funder_address = "",
        const std::optional<ApiCredentials> &api_credentials = std::nullopt);

} // namespace polymarket::arb
