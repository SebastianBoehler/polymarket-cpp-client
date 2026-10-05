#pragma once

#include "polymarket/order_signer.hpp"

namespace polymarket::detail
{
    void validate_api_credentials(const ApiCredentials &credentials);
}
