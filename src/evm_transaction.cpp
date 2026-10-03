#include "evm_transaction.hpp"
#include "evm_rlp.hpp"
#include "evm_uint.hpp"
#include "order_signer.hpp"
#include <limits>
#include <stdexcept>

namespace polymarket
{
    namespace
    {
        std::vector<uint8_t> parse_hex_field(const std::string &hex, const char *label)
        {
            try
            {
                return from_hex(hex);
            }
            catch (const std::invalid_argument &)
            {
                throw std::invalid_argument(std::string("transaction ") + label + " is not valid hex");
            }
        }

        std::vector<uint8_t> encode_uint_field(const std::string &text, const char *label)
        {
            try
            {
                return detail::rlp_encode_uint(detail::parse_uint256_word(text));
            }
            catch (const std::invalid_argument &error)
            {
                throw std::invalid_argument(std::string("transaction ") + label + ": " + error.what());
            }
        }

        std::vector<std::vector<uint8_t>> unsigned_fields(const EvmLegacyTransaction &tx)
        {
            if (tx.chain_id == 0 || tx.chain_id > (std::numeric_limits<uint64_t>::max() - 36) / 2)
                throw std::invalid_argument("transaction chain_id is out of range");
            const auto to = parse_hex_field(tx.to, "to");
            if (to.size() != 20)
                throw std::invalid_argument("transaction to must be a 20-byte address");

            return {encode_uint_field(tx.nonce, "nonce"),
                    encode_uint_field(tx.gas_price, "gas_price"),
                    encode_uint_field(tx.gas_limit, "gas_limit"),
                    detail::rlp_encode_bytes(to),
                    encode_uint_field(tx.value, "value"),
                    detail::rlp_encode_bytes(parse_hex_field(tx.data, "data"))};
        }
    } // namespace

    std::array<uint8_t, 32> evm_legacy_transaction_signing_hash(const EvmLegacyTransaction &tx)
    {
        auto fields = unsigned_fields(tx);
        fields.push_back(detail::rlp_encode_uint(detail::uint256_word(tx.chain_id)));
        fields.push_back(detail::rlp_encode_uint({}));
        fields.push_back(detail::rlp_encode_uint({}));
        return keccak256(detail::rlp_encode_list(fields));
    }

    EvmSignedTransaction evm_sign_legacy_transaction(OrderSigner &signer,
                                                     const EvmLegacyTransaction &tx)
    {
        const auto hash = evm_legacy_transaction_signing_hash(tx);
        const auto signature = from_hex(signer.sign_hash(hash)); // r || s || (27 + recovery id)
        if (signature.size() != 65 || (signature[64] != 27 && signature[64] != 28))
            throw std::runtime_error("signer returned an unexpected signature layout");

        const uint64_t recovery_id = signature[64] - 27u;
        const uint64_t v = tx.chain_id * 2 + 35 + recovery_id;
        auto fields = unsigned_fields(tx);
        fields.push_back(detail::rlp_encode_uint(detail::uint256_word(v)));
        fields.push_back(detail::rlp_encode_uint({signature.begin(), signature.begin() + 32}));
        fields.push_back(detail::rlp_encode_uint({signature.begin() + 32, signature.begin() + 64}));
        const auto raw = detail::rlp_encode_list(fields);
        return {to_hex(raw), to_hex(keccak256(raw))};
    }

} // namespace polymarket
