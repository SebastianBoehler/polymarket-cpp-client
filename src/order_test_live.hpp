#pragma once

#include "order_signer.hpp"

#include <string>

namespace polymarket::order_test
{
    inline constexpr char CLOB_API[] = "https://clob.polymarket.com";
    inline constexpr char NEG_RISK_CTF_EXCHANGE[] = "0xe2222d279d744050d28e00520010520000310F59";
    inline constexpr char CTF_EXCHANGE[] = "0xE111180000d2663C0091e4f400237545B87B996B";

    bool run_live_order(const std::string &private_key,
                        const std::string &funder_address,
                        const ApiCredentials &credentials,
                        bool have_credentials,
                        const OrderSigner &signer);
}
