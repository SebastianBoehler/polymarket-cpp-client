#include "order_signer.hpp"
#include <iostream>
#include <cstdlib>

int main()
{
    using namespace polymarket;

    const char *pk_env = std::getenv("PRIVATE_KEY");
    if (!pk_env)
    {
        std::cout << "PRIVATE_KEY not set; skipping signing example.\n";
        return 0;
    }

    std::string private_key = pk_env;

    try
    {
        OrderSigner signer(private_key, 137);
        std::cout << "Address: " << signer.address() << "\n";

        OrderData order;
        order.maker = signer.address();
        order.taker = "0x0000000000000000000000000000000000000000";
        order.token_id = "1234567890";
        order.maker_amount = to_wei(1.0, 6); // $1
        order.taker_amount = to_wei(2.0, 6); // 2 shares (dummy)
        order.side = OrderSide::BUY;
        order.fee_rate_bps = "0";
        order.nonce = "0";
        order.signer = signer.address();
        order.expiration = "0";
        order.signature_type = SignatureType::EOA;

        auto signed_order = signer.sign_order(order, "0x4bFb41d5B3570DeFd03C39a9A4D8dE6Bd8B8982E");
        std::cout << "Salt: " << signed_order.salt << "\n";
        std::cout << "Signature: " << signed_order.signature.substr(0, 20) << "...\n";
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
