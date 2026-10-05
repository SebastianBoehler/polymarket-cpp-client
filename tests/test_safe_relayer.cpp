// Safe signing and relayer payload vectors from the official py-sdk:
// derive_safe_wallet_address, encode_safe_multisend_call,
// sign_safe_transaction and build_safe_payload.
#include "safe_relayer.hpp"
#include "position_test_support.hpp"

using namespace position_test;

namespace
{
    const std::string kSigner = kWallet;

    std::vector<ContractCall> multisend_calls()
    {
        return {{kContracts.collateral_adapter, kRedeemData, "0"},
                {"0x000000000000000000000000000000000000dEaD", "0xabcdef", "5"}};
    }

    void official_vectors()
    {
        expect_equal("test Safe", detail::derive_safe_address(kSigner, kContracts), kSafe);
        expect_equal("production Safe of a real Polymarket account",
                     detail::derive_safe_address("0xAde4611dF7a34071A1886503f2Ab7D2bc1C68bC9", kContracts),
                     "0xa8Beb4744dc06f51355b92baa9a57dd7F127D006");

        OrderSigner signer(kKey);
        const detail::SafeCall single{{kContracts.collateral_adapter, kRedeemData, "0"}, detail::kSafeOperationCall};
        const auto digest = detail::safe_transaction_digest(kSafe, 137, single, "12");
        expect_equal("single-call SafeTx digest", to_hex(digest),
                     "0x1c9a8453e6198cefee86e8927b6043a5801560a1a52bd4bfc30fc6d7a845b36e");
        const auto signature = detail::sign_safe_digest(signer, digest);
        expect_equal("single-call Safe signature", signature, kSingleSignature);

        const auto payload = detail::build_safe_payload(kSigner, kSafe, single, "12", signature,
                                                        "Redeem positions for condition 0x6b04");
        const auto expected_payload = nlohmann::json::parse(
            R"({"type":"SAFE","from":"0x9d8A62f656a8d1615C1294fd71e9CFb3E4855A4F","to":"0xAdA100Db00Ca00073811820692005400218FcE1f",)"
            R"("proxyWallet":"0xe909E402b7FA6E29D2f91b343B44Ed29C1d498Ed","data":")" +
            kRedeemData + R"(","nonce":"12","signature":")" + kSingleSignature +
            R"(","metadata":"Redeem positions for condition 0x6b04","signatureParams":{"baseGas":"0","gasPrice":"0",)"
            R"("gasToken":"0x0000000000000000000000000000000000000000","operation":"0",)"
            R"("refundReceiver":"0x0000000000000000000000000000000000000000","safeTxnGas":"0"}})");
        check(payload == expected_payload, "submit payload mismatch:\n  " + payload.dump() + "\n  " + expected_payload.dump());

        const auto batch = detail::resolve_safe_call(multisend_calls(), kContracts);
        check(batch.operation == detail::kSafeOperationDelegateCall && batch.call.to == kContracts.safe_multisend,
              "batch is a DELEGATECALL into MultiSend");
        expect_equal("MultiSend packing", batch.call.data,
                     "0x8d80ff0a00000000000000000000000000000000000000000000000000000000000000200000000000000000000000"
                     "00000000000000000000000000000000000000019100ada100db00ca00073811820692005400218fce1f000000000000"
                     "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
                     "000000000000000000e401b7037c000000000000000000000000c011a7e12a19f7b1f670d46f03b03f3342e82dfb0000"
                     "0000000000000000000000000000000000000000000000000000000000006b049bc3befa02c22d0cfaee77625fc94e9a"
                     "6d571f00c015ba7a3b442e306fbc00000000000000000000000000000000000000000000000000000000000000800000"
                     "000000000000000000000000000000000000000000000000000000000002000000000000000000000000000000000000"
                     "000000000000000000000000000100000000000000000000000000000000000000000000000000000000000000020000"
                     "0000000000000000000000000000000000dead0000000000000000000000000000000000000000000000000000000000"
                     "0000050000000000000000000000000000000000000000000000000000000000000003abcdef00000000000000000000"
                     "0000000000");
        const auto batch_digest = detail::safe_transaction_digest(kSafe, 137, batch, "0");
        expect_equal("MultiSend SafeTx digest", to_hex(batch_digest),
                     "0x14e1b5b6845ccb56b46a0a288d3af9085534f67bb84afbe3a69ab532b42a04c3");
        expect_equal("MultiSend Safe signature", detail::sign_safe_digest(signer, batch_digest),
                     "0x706e5fa20a46a6af76dd3ed7e20f1a248b9d9469958bb55e36105b5fbf04ad8609deed4cb77bf83118980d8f5266f3"
                     "ec1be740a9838f5b32c5cf759e447d8ab21f");
        check(!detail::build_safe_payload(kSigner, kSafe, batch, "0", "0x", "").contains("value"),
              "zero value is omitted from the payload");
        check(detail::build_safe_payload(kSigner, kSafe, {{kSafe, "0x", "7"}, 0}, "0", "0x", "").at("value") == "7",
              "non-zero value is sent");
    }

    void retry_rules()
    {
        const auto retryable = [](long status, const std::string &body)
        { return detail::is_retryable_submit_error(detail::RelayerRequestError("/submit", status, body)); };
        check(retryable(429, ""), "rate limit is retryable");
        check(retryable(400, R"({"error":"Wallet busy: has active action"})"), "busy wallet is retryable");
        check(retryable(400, "wallet has in-flight action"), "in-flight action is retryable");
        check(retryable(400, "batch nonce 4 does not match on-chain nonce 12"), "stale nonce is retryable");
        check(!retryable(400, "batch nonce 12 does not match on-chain nonce 4"), "future nonce is not retryable");
        check(!retryable(400, "invalid signature"), "bad signature is not retryable");
        check(!retryable(401, "wallet busy: active action"), "auth failure is not retryable");
        check(!retryable(500, ""), "server error is not retryable");
    }
} // namespace

int main()
{
    official_vectors();
    retry_rules();
    return check_support::finish("test_safe_relayer");
}
