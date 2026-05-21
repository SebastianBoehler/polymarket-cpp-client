#include "order_signer.hpp"

#include <iostream>
#include <string>

using namespace polymarket;

namespace
{
    constexpr const char *kPrivateKey = "0x0000000000000000000000000000000000000000000000000000000000000001";
    constexpr const char *kSigner = "0x7E5F4552091A69125d5DfCb7b8C2659029395Bdf";
    constexpr const char *kExchangeV2 = "0xE111180000d2663C0091e4f400237545B87B996B";
    constexpr const char *kZeroBytes32 = "0x0000000000000000000000000000000000000000000000000000000000000000";

    bool expect_equal(const std::string &name, const std::string &actual, const std::string &expected)
    {
        if (actual == expected)
        {
            return true;
        }

        std::cerr << name << " mismatch\n"
                  << "  expected: " << expected << "\n"
                  << "  actual:   " << actual << "\n";
        return false;
    }

    OrderData base_order(SignatureType signature_type)
    {
        OrderData order;
        order.maker = signature_type == SignatureType::POLY_1271
                          ? "0x1111111111111111111111111111111111111111"
                          : kSigner;
        order.signer = signature_type == SignatureType::POLY_1271 ? order.maker : kSigner;
        order.taker = "0x0000000000000000000000000000000000000000";
        order.token_id = "1234567890";
        order.maker_amount = "1000000";
        order.taker_amount = "2000000";
        order.side = OrderSide::BUY;
        order.expiration = "0";
        order.timestamp = "1713398400000";
        order.metadata = kZeroBytes32;
        order.builder = kZeroBytes32;
        order.signature_type = signature_type;
        return order;
    }
}

int main()
{
    OrderSigner signer(kPrivateKey, 137);

    if (!expect_equal("derived address", signer.address(), kSigner))
    {
        return 1;
    }

    constexpr const char *kFunder = "0x1111111111111111111111111111111111111111";
    const auto l1_headers = signer.generate_l1_headers(0, kFunder);
    if (!expect_equal("L1 auth funder address", l1_headers.poly_address, kFunder))
    {
        return 1;
    }

    ApiCredentials creds;
    creds.api_key = "test-key";
    creds.api_secret = "c2VjcmV0";
    creds.api_passphrase = "test-passphrase";
    const auto l2_headers = signer.generate_l2_headers(creds, "GET", "/orders", "", kFunder);
    if (!expect_equal("L2 auth funder address", l2_headers.poly_address, kFunder))
    {
        return 1;
    }

    const auto eoa = signer.sign_order_with_salt(base_order(SignatureType::EOA), kExchangeV2, "123456789");
    if (!expect_equal("EOA V2 signature", eoa.signature,
                      "0x92daffe6e8b80fb13506e91647e066ff58d3f7050021043fac41a24990b279e33f3624578b7f0fc5a59b225d2d96f063c09147308e936d5a42527e369aff3d4a1c"))
    {
        return 1;
    }

    const auto proxy = signer.sign_order_with_salt(base_order(SignatureType::POLY_PROXY), kExchangeV2, "123456789");
    if (!expect_equal("POLY_PROXY V2 signature", proxy.signature,
                      "0x338b9a50123137f328088f37b9b9f6344c4379d36ce4c22dd187aa23fed246a647b6ce7945addd2f46b74544a5ca0dc52d2ccc0e98d5d0b4c5606607ca2b9ac01b"))
    {
        return 1;
    }

    const auto poly1271 = signer.sign_order_with_salt(base_order(SignatureType::POLY_1271), kExchangeV2, "123456789");
    if (!expect_equal("POLY_1271 V2 signature", poly1271.signature,
                      "0xd327d1a286044bf0b31af12d501452c6973652c594a585e808c009a9a1d1f34a4a02fa71b763286969d19ebe562b690e48c7db478fdbfe04e3d02a8e7127c6201b3264e159346253e26a64e00b69032db0e7d32f94628de3e6eecb50304d7af3d29ee95ac9f21b67f465df2230cc6aa22378e0f5525f0745619cc52937ea190ae64f726465722875696e743235362073616c742c61646472657373206d616b65722c61646472657373207369676e65722c75696e7432353620746f6b656e49642c75696e74323536206d616b6572416d6f756e742c75696e743235362074616b6572416d6f756e742c75696e743820736964652c75696e7438207369676e6174757265547970652c75696e743235362074696d657374616d702c62797465733332206d657461646174612c62797465733332206275696c6465722900ba"))
    {
        return 1;
    }

    return 0;
}
