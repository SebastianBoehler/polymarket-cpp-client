#pragma once

#include "polymarket/environment.hpp"
#include "polymarket/order_signer.hpp"

#include <cstdlib>
#include <string>

namespace polymarket::order_test
{
    // POLYMARKET_ENV selects "production" (default) or "preproduction".
    inline Environment selected_environment()
    {
        const char *name = std::getenv("POLYMARKET_ENV");
        return Environment::from_name(name && *name ? name : "production");
    }

    bool run_live_order(const Environment &environment, const std::string &private_key,
                        const std::string &funder_address, const ApiCredentials &credentials,
                        bool have_credentials, const OrderSigner &signer);
}
