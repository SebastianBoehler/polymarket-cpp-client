#include "order_signer.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace polymarket
{
    namespace
    {
        const std::string BYTES32_ZERO = "0x0000000000000000000000000000000000000000000000000000000000000000";
        const std::string ZERO_ADDRESS = "0x0000000000000000000000000000000000000000";
        const std::string V2_ORDER_TYPE =
            "Order(uint256 salt,address maker,address signer,uint256 tokenId,"
            "uint256 makerAmount,uint256 takerAmount,uint8 side,uint8 signatureType,"
            "uint256 timestamp,bytes32 metadata,bytes32 builder)";

        std::vector<uint8_t> encode_uint256(const std::string &value)
        {
            std::vector<uint8_t> result(32, 0);
            if (value.empty())
                return result;

            if (value.size() >= 2 && value.substr(0, 2) == "0x")
            {
                auto bytes = from_hex(value);
                if (bytes.size() > result.size())
                    throw std::invalid_argument("uint256 value exceeds 32 bytes");
                const size_t offset = result.size() - bytes.size();
                std::memcpy(result.data() + offset, bytes.data(), bytes.size());
                return result;
            }

            std::string num = value;
            if (!std::all_of(num.begin(), num.end(), [](char digit)
                             { return digit >= '0' && digit <= '9'; }))
            {
                throw std::invalid_argument("uint256 value must be an unsigned decimal integer");
            }
            std::vector<uint8_t> bytes;
            while (!num.empty() && num != "0")
            {
                int remainder = 0;
                std::string quotient;
                for (char c : num)
                {
                    int digit = remainder * 10 + (c - '0');
                    if (!quotient.empty() || digit / 256 > 0)
                        quotient += static_cast<char>('0' + digit / 256);
                    remainder = digit % 256;
                }
                bytes.push_back(static_cast<uint8_t>(remainder));
                num = quotient.empty() ? "0" : quotient;
            }
            if (bytes.size() > result.size())
                throw std::invalid_argument("uint256 value exceeds 32 bytes");
            for (size_t i = 0; i < bytes.size(); i++)
                result[31 - i] = bytes[i];
            return result;
        }

        std::vector<uint8_t> encode_address(const std::string &addr)
        {
            auto bytes = from_hex(addr);
            if (bytes.size() != 20)
                throw std::invalid_argument("order address must be exactly 20 bytes");
            std::vector<uint8_t> result(32, 0);
            std::memcpy(result.data() + 12, bytes.data(), bytes.size());
            return result;
        }

        std::vector<uint8_t> encode_bytes32(const std::string &value)
        {
            auto bytes = from_hex(value.empty() ? BYTES32_ZERO : value);
            if (bytes.size() != 32)
                throw std::invalid_argument("bytes32 value must be exactly 32 bytes");
            return bytes;
        }

        std::string strip_0x(const std::string &hex)
        {
            if (hex.size() >= 2 && (hex.substr(0, 2) == "0x" || hex.substr(0, 2) == "0X"))
                return hex.substr(2);
            return hex;
        }

        std::string string_to_hex_no_prefix(const std::string &text)
        {
            std::ostringstream oss;
            for (unsigned char c : text)
                oss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(c);
            return oss.str();
        }

        std::string current_time_millis()
        {
            auto now = std::chrono::system_clock::now().time_since_epoch();
            auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
            return std::to_string(millis);
        }

        void validate_order_enums(const OrderData &order)
        {
            if (order.side != OrderSide::BUY && order.side != OrderSide::SELL)
                throw std::invalid_argument("order side must be BUY or SELL");
            if (order.signature_type != SignatureType::EOA &&
                order.signature_type != SignatureType::POLY_PROXY &&
                order.signature_type != SignatureType::POLY_GNOSIS_SAFE &&
                order.signature_type != SignatureType::POLY_1271)
                throw std::invalid_argument("unsupported order signature type");
        }
    }

    std::array<uint8_t, 32> OrderSigner::hash_order_v2(const OrderData &order, const std::string &salt)
    {
        std::vector<uint8_t> encoded;
        static const auto type_hash = keccak256(V2_ORDER_TYPE);
        encoded.insert(encoded.end(), type_hash.begin(), type_hash.end());

        const auto fields = {
            encode_uint256(salt),
            encode_address(order.maker),
            encode_address(order.signer),
            encode_uint256(order.token_id),
            encode_uint256(order.maker_amount),
            encode_uint256(order.taker_amount),
        };
        for (const auto &field : fields)
            encoded.insert(encoded.end(), field.begin(), field.end());

        std::vector<uint8_t> side_enc(32, 0);
        side_enc[31] = static_cast<uint8_t>(order.side);
        encoded.insert(encoded.end(), side_enc.begin(), side_enc.end());

        std::vector<uint8_t> sig_type_enc(32, 0);
        sig_type_enc[31] = static_cast<uint8_t>(order.signature_type);
        encoded.insert(encoded.end(), sig_type_enc.begin(), sig_type_enc.end());

        const auto trailing = {
            encode_uint256(order.timestamp),
            encode_bytes32(order.metadata),
            encode_bytes32(order.builder),
        };
        for (const auto &field : trailing)
            encoded.insert(encoded.end(), field.begin(), field.end());

        return keccak256(encoded);
    }

    SignedOrder OrderSigner::sign_order(const OrderData &order, const std::string &exchange_address)
    {
        return sign_order_with_salt(order, exchange_address, generate_salt());
    }

    SignedOrder OrderSigner::sign_order_with_salt(const OrderData &order, const std::string &exchange_address, const std::string &salt)
    {
        validate_order_enums(order);
        OrderData resolved = order;
        if (resolved.taker.empty())
            resolved.taker = ZERO_ADDRESS;
        if (resolved.expiration.empty())
            resolved.expiration = "0";
        if (resolved.timestamp.empty())
            resolved.timestamp = current_time_millis();
        if (resolved.metadata.empty())
            resolved.metadata = BYTES32_ZERO;
        if (resolved.builder.empty())
            resolved.builder = BYTES32_ZERO;
        if (resolved.signer.empty())
            resolved.signer = resolved.maker;

        auto domain_hash = v2_domain_hash(exchange_address);
        auto order_hash = hash_order_v2(resolved, salt);
        std::string signature;

        if (resolved.signature_type == SignatureType::POLY_1271)
        {
            signature = sign_poly_1271(domain_hash, order_hash, resolved.signer);
        }
        else
        {
            if (resolved.signer != address_)
                throw std::runtime_error("signer does not match private key address");
            signature = sign_hash(encode_eip712(domain_hash, order_hash));
        }

        SignedOrder signed_order;
        signed_order.salt = salt;
        signed_order.maker = resolved.maker;
        signed_order.signer = resolved.signer;
        signed_order.taker = resolved.taker;
        signed_order.token_id = resolved.token_id;
        signed_order.maker_amount = resolved.maker_amount;
        signed_order.taker_amount = resolved.taker_amount;
        signed_order.expiration = resolved.expiration;
        signed_order.nonce = resolved.nonce;
        signed_order.fee_rate_bps = resolved.fee_rate_bps;
        signed_order.side = static_cast<int>(resolved.side);
        signed_order.signature_type = static_cast<int>(resolved.signature_type);
        signed_order.timestamp = resolved.timestamp;
        signed_order.metadata = resolved.metadata;
        signed_order.builder = resolved.builder;
        signed_order.signature = signature;
        return signed_order;
    }

    std::string OrderSigner::sign_poly_1271(const std::array<uint8_t, 32> &domain_hash,
                                            const std::array<uint8_t, 32> &order_hash,
                                            const std::string &verifying_contract)
    {
        auto solady_type_hash = keccak256(
            "TypedDataSign(Order contents,string name,string version,uint256 chainId,"
            "address verifyingContract,bytes32 salt)" +
            V2_ORDER_TYPE);
        auto name_hash = keccak256("DepositWallet");
        auto version_hash = keccak256("1");

        std::vector<uint8_t> encoded;
        encoded.insert(encoded.end(), solady_type_hash.begin(), solady_type_hash.end());
        encoded.insert(encoded.end(), order_hash.begin(), order_hash.end());
        encoded.insert(encoded.end(), name_hash.begin(), name_hash.end());
        encoded.insert(encoded.end(), version_hash.begin(), version_hash.end());

        auto chain_id_enc = encode_uint256(std::to_string(chain_id_));
        auto verifying_contract_enc = encode_address(verifying_contract);
        auto salt_enc = encode_bytes32(BYTES32_ZERO);
        encoded.insert(encoded.end(), chain_id_enc.begin(), chain_id_enc.end());
        encoded.insert(encoded.end(), verifying_contract_enc.begin(), verifying_contract_enc.end());
        encoded.insert(encoded.end(), salt_enc.begin(), salt_enc.end());

        auto solady_struct_hash = keccak256(encoded);
        auto inner_signature = sign_hash(encode_eip712(domain_hash, solady_struct_hash));

        std::ostringstream len_hex;
        len_hex << std::hex << std::setfill('0') << std::setw(4) << V2_ORDER_TYPE.size();

        return "0x" + strip_0x(inner_signature) +
               strip_0x(to_hex(domain_hash)) +
               strip_0x(to_hex(order_hash)) +
               string_to_hex_no_prefix(V2_ORDER_TYPE) +
               len_hex.str();
    }
}
