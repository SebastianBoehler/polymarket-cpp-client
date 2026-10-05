#include "polymarket/evm_abi.hpp"
#include "evm_rlp.hpp"
#include "polymarket/evm_transaction.hpp"
#include "polymarket/order_signer.hpp"
#include <cstdint>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

// Signed-transaction vectors come from eth_account 0.13 (Account.sign_transaction)
// with the EIP-155 example key 0x4646...46. The first case is the worked example
// from the EIP-155 specification.

namespace
{
    using namespace polymarket;

    int failures = 0;

    void expect_equal(const std::string &name, const std::string &actual, const std::string &expected)
    {
        if (actual == expected)
            return;
        ++failures;
        std::cerr << name << " mismatch\n"
                  << "  expected: " << expected << "\n"
                  << "  actual:   " << actual << "\n";
    }

    void expect_throws(const std::string &name, const std::function<void()> &action)
    {
        try
        {
            action();
        }
        catch (const std::invalid_argument &)
        {
            return;
        }
        catch (const std::exception &error)
        {
            ++failures;
            std::cerr << name << " threw the wrong exception type: " << error.what() << "\n";
            return;
        }
        ++failures;
        std::cerr << name << " did not throw\n";
    }

    std::vector<uint8_t> text_bytes(const std::string &text)
    {
        return {text.begin(), text.end()};
    }

    void rlp_vectors()
    {
        using namespace polymarket::detail;
        expect_equal("rlp dog", to_hex(rlp_encode_bytes(text_bytes("dog"))), "0x83646f67");
        expect_equal("rlp empty string", to_hex(rlp_encode_bytes({})), "0x80");
        expect_equal("rlp single low byte", to_hex(rlp_encode_bytes({0x00})), "0x00");
        expect_equal("rlp single high byte", to_hex(rlp_encode_bytes({0x80})), "0x8180");
        expect_equal("rlp zero", to_hex(rlp_encode_uint({0, 0, 0})), "0x80");
        expect_equal("rlp 15", to_hex(rlp_encode_uint({0, 0x0f})), "0x0f");
        expect_equal("rlp 1024", to_hex(rlp_encode_uint({0, 0, 0x04, 0x00})), "0x820400");
        expect_equal("rlp empty list", to_hex(rlp_encode_list({})), "0xc0");
        expect_equal("rlp cat dog",
                     to_hex(rlp_encode_list({rlp_encode_bytes(text_bytes("cat")),
                                             rlp_encode_bytes(text_bytes("dog"))})),
                     "0xc88363617483646f67");
        expect_equal("rlp 56-byte string",
                     to_hex(rlp_encode_bytes(text_bytes("Lorem ipsum dolor sit amet, consectetur adipisicing elit"))),
                     "0xb8384c6f72656d20697073756d20646f6c6f722073697420616d65742c20636f6e7365637465747572"
                     "20616469706973696369" "6e6720656c6974");
        expect_equal("rlp 1024-byte string header",
                     to_hex(rlp_encode_bytes(std::vector<uint8_t>(1024, 0xaa))).substr(0, 10),
                     "0xb90400aa");
    }

    struct SignedCase
    {
        std::string name;
        EvmLegacyTransaction tx;
        std::string signing_hash;
        std::string raw;
        std::string hash;
    };

    std::string split_calldata()
    {
        return evm_abi_encode_call(
            "splitPosition(address,bytes32,bytes32,uint256[],uint256)",
            {EvmAbiValue::address("0x2791Bca1f2de4661ED88A30C99A7a9449Aa84174"),
             EvmAbiValue::bytes32("0x" + std::string(64, '0')),
             EvmAbiValue::bytes32("0x" + std::string(64, '1')),
             EvmAbiValue::array({EvmAbiValue::uint256(1), EvmAbiValue::uint256(2)}),
             EvmAbiValue::uint256(1000000)});
    }

    std::string long_data()
    {
        std::vector<uint8_t> data;
        for (int i = 0; i < 256; ++i)
            data.push_back(static_cast<uint8_t>(i));
        for (int i = 0; i < 44; ++i)
            data.push_back(static_cast<uint8_t>(i));
        return to_hex(data);
    }

    void signed_transactions()
    {
        OrderSigner signer("4646464646464646464646464646464646464646464646464646464646464646");
        expect_equal("example key address", signer.address(), "0x9d8A62f656a8d1615C1294fd71e9CFb3E4855A4F");

        const std::vector<SignedCase> cases = {
            {"eip155 spec example",
             {"9", "20000000000", "21000", "0x3535353535353535353535353535353535353535",
              "1000000000000000000", "0x", 1},
             "0xdaf5a779ae972f972197303d7b574746c7ef83eadac0f2791ad23db92e4c8e53",
             "0xf86c098504a817c800825208943535353535353535353535353535353535353535880de0b6b3a7640000"
             "8025a028ef61340bd939bc2195fe537567866003e1a15d3c71ff63e1590620aa636276a067cbe9d8997f"
             "761aecb703304b3800ccf555c9f3dc64214b297fb1966a3b6d83",
             "0x33469b22e9f636356c4160a87eb19df52b7412e8eac32a4a55ffe88ea8350788"},
            {"polygon split via collateral adapter, hex quantities",
             {"0x4d2", "0x7558bdb00", "0x3d090", "0xAdA100Db00Ca00073811820692005400218FcE1f",
              "0x0", split_calldata(), 137},
             "0x23012eb18496ebc3a7a2615ea806e6e2fab24c50e75bce44b353a47fd8ecc565",
             "0xf9016f8204d28507558bdb008303d09094ada100db00ca00073811820692005400218fce1f80b90104"
             "72ce42750000000000000000000000002791bca1f2de4661ed88a30c99a7a9449aa84174000000000000"
             "0000000000000000000000000000000000000000000000000000111111111111111111111111111111111111"
             "111111111111111111111111111100000000000000000000000000000000000000000000000000000000000000"
             "a000000000000000000000000000000000000000000000000000000000000f42400000000000000000000000"
             "0000000000000000000000000000000000000000020000000000000000000000000000000000000000000000"
             "0000000000000000010000000000000000000000000000000000000000000000000000000000000002820135"
             "a0aa8e93fd017c273082e1c84cbf9eec6e3d609461f035b2a18dc8d722026c2152a0542912b9925100ed38f0"
             "871914126ec43f5471057ec644ea8c14d71fda3e58fe",
             "0x13d47ea6c671e13409fdd618f7cdedc36eafca7d253eea8bca748cbff2006a24"},
            {"zero nonce and value",
             {"0", "1", "21000", "0x000000000000000000000000000000000000dEaD", "0", "0x", 137},
             "0xf5a8c39c413120b43a7e2c73e5827ee42db674d231b86b933bd99c02e64371ac",
             "0xf861800182520894000000000000000000000000000000000000dead8080820136a0f8c480cf84f1fcecaf"
             "5beae60d9827467d91c78b7f283b0d7d423b0bc1bc7a48a05265e2ed4b58056ea433867f38bc2f4d77a62a84"
             "304b83a05078f3c0397f6c41",
             "0x8ce2b87acda51a982092d1583dce2a641eef54c874df7c8a871d77a86f3283f2"},
            {"long data, wide values, Amoy chain id",
             {"127", "18446744073709551621", "10000000", "0x4D97DCd97eC945f40cF65F87097ACe5EA0476045",
              "0x1" + std::string(50, '0'), long_data(), 80002},
             "0x106d16da445c8bca44d5d1d039c2810e5e08dac97481a7dc3e253db7f4b73f73",
             "0xf901b47f8901000000000000000583989680944d97dcd97ec945f40cf65f87097ace5ea04760459a010000"
             "0000000000000000000000000000000000000000000000b9012c000102030405060708090a0b0c0d0e0f1011"
             "12131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d"
             "3e3f404142434445464748494a4b4c4d4e4f505152535455565758595a5b5c5d5e5f60616263646566676869"
             "6a6b6c6d6e6f707172737475767778797a7b7c7d7e7f808182838485868788898a8b8c8d8e8f909192939495"
             "969798999a9b9c9d9e9fa0a1a2a3a4a5a6a7a8a9aaabacadaeafb0b1b2b3b4b5b6b7b8b9babbbcbdbebfc0c1"
             "c2c3c4c5c6c7c8c9cacbcccdcecfd0d1d2d3d4d5d6d7d8d9dadbdcdddedfe0e1e2e3e4e5e6e7e8e9eaebeced"
             "eeeff0f1f2f3f4f5f6f7f8f9fafbfcfdfeff000102030405060708090a0b0c0d0e0f10111213141516171819"
             "1a1b1c1d1e1f202122232425262728292a2b83027128a071b0eed65b7e6e414c5ad5d25315225a2e9c7fa28e"
             "e030704ba4d1e0aeca2919a016169a5d43df0a4596d4bc9b4ac85d6dfb02971b679a6ae18bfece24790e5c9f",
             "0xf555df3464c442e4ea2f30f15de7172e5f2e8bfea2772e2e23edf41ce7fc67da"},
        };

        for (const auto &c : cases)
        {
            expect_equal(c.name + " signing hash", to_hex(evm_legacy_transaction_signing_hash(c.tx)),
                         c.signing_hash);
            const auto signed_tx = evm_sign_legacy_transaction(signer, c.tx);
            expect_equal(c.name + " raw", signed_tx.raw_transaction, c.raw);
            expect_equal(c.name + " hash", signed_tx.transaction_hash, c.hash);
        }
    }

    void rejections()
    {
        const EvmLegacyTransaction valid{"0", "1", "21000", "0x000000000000000000000000000000000000dEaD",
                                         "0", "0x", 137};
        const auto rejects = [&](const std::string &name, const std::function<void(EvmLegacyTransaction &)> &mutate)
        {
            auto tx = valid;
            mutate(tx);
            expect_throws(name, [&]
                          { (void)evm_legacy_transaction_signing_hash(tx); });
        };
        rejects("chain id zero", [](auto &tx)
                { tx.chain_id = 0; });
        rejects("chain id overflowing v", [](auto &tx)
                { tx.chain_id = UINT64_MAX / 2; });
        rejects("empty nonce", [](auto &tx)
                { tx.nonce.clear(); });
        rejects("negative gas price", [](auto &tx)
                { tx.gas_price = "-1"; });
        rejects("value over uint256", [](auto &tx)
                { tx.value = "0x1" + std::string(64, '0'); });
        rejects("contract creation", [](auto &tx)
                { tx.to.clear(); });
        rejects("short to", [](auto &tx)
                { tx.to = "0xdead"; });
        rejects("odd data", [](auto &tx)
                { tx.data = "0xabc"; });
        rejects("non-hex data", [](auto &tx)
                { tx.data = "0xzz"; });
    }
} // namespace

int main()
{
    rlp_vectors();
    signed_transactions();
    rejections();
    if (failures != 0)
    {
        std::cerr << failures << " test_evm_transaction check(s) failed\n";
        return 1;
    }
    std::cout << "test_evm_transaction passed\n";
    return 0;
}
