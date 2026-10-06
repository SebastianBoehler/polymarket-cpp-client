#pragma once

#include "polymarket/environment.hpp"
#include "polymarket/order_signer.hpp"

#include <string>

namespace polymarket::order_test
{
    bool run_live_order(const Environment &environment, const std::string &private_key,
                        const std::string &funder_address, const ApiCredentials &credentials,
                        bool have_credentials, const OrderSigner &signer);
}
