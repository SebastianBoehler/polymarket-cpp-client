#include "order_signer.hpp"
#include "http_client.hpp"
#include <secp256k1.h>
#include <secp256k1_recovery.h>
#include <ethash/keccak.hpp>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <nlohmann/json.hpp>
#include <random>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <stdexcept>
#include <chrono>

using json = nlohmann::json;

namespace polymarket
{

    std::string to_hex(const std::vector<uint8_t> &data)
    {
        std::stringstream ss;
        ss << "0x";
        for (auto b : data)
        {
            ss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(b);
        }
        return ss.str();
    }

    std::string to_hex(const std::array<uint8_t, 32> &data)
    {
        std::stringstream ss;
        ss << "0x";
        for (auto b : data)
        {
            ss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(b);
        }
        return ss.str();
    }

    std::vector<uint8_t> from_hex(const std::string &hex)
    {
        std::string h = hex;
        if (h.substr(0, 2) == "0x" || h.substr(0, 2) == "0X")
        {
            h = h.substr(2);
        }
        std::vector<uint8_t> result;
        result.reserve(h.size() / 2);
        for (size_t i = 0; i < h.size(); i += 2)
        {
            uint8_t byte = static_cast<uint8_t>(std::stoi(h.substr(i, 2), nullptr, 16));
            result.push_back(byte);
        }
        return result;
    }

    std::array<uint8_t, 32> keccak256(const std::vector<uint8_t> &data)
    {
        auto hash = ethash::keccak256(data.data(), data.size());
        std::array<uint8_t, 32> result;
        std::memcpy(result.data(), hash.bytes, 32);
        return result;
    }

    std::array<uint8_t, 32> keccak256(const std::string &data)
    {
        std::vector<uint8_t> bytes(data.begin(), data.end());
        return keccak256(bytes);
    }

    std::string to_wei(double amount, int decimals, bool round_down)
    {
        long double multiplier = 1;
        for (int i = 0; i < decimals; i++)
            multiplier *= 10;
        long double wei_ld = amount * multiplier;
        uint64_t wei;
        if (round_down)
        {
            wei = static_cast<uint64_t>(wei_ld); // Truncate (floor for positive numbers)
        }
        else
        {
            wei = static_cast<uint64_t>(wei_ld + 0.5); // Round to nearest
        }
        return std::to_string(wei);
    }

    std::string generate_salt()
    {
        // Generate a random decimal number (like TS client does)
        std::random_device rd;
        std::mt19937_64 gen(rd());
        std::uniform_int_distribution<uint64_t> dis(0, 999999999999ULL);
        return std::to_string(dis(gen));
    }

    static std::vector<uint8_t> base64_decode(const std::string &encoded)
    {
        // Support both standard (+/) and URL-safe (-_) base64
        static const std::string base64_chars =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::vector<uint8_t> result;
        std::vector<int> T(256, -1);
        for (int i = 0; i < 64; i++)
            T[base64_chars[i]] = i;
        // Also map URL-safe variants
        T['-'] = 62;
        T['_'] = 63;
        int val = 0, valb = -8;
        for (unsigned char c : encoded)
        {
            if (c == '=')
                break;
            if (T[c] == -1)
                continue;
            val = (val << 6) + T[c];
            valb += 6;
            if (valb >= 0)
            {
                result.push_back((val >> valb) & 0xFF);
                valb -= 8;
            }
        }
        return result;
    }

    static std::string base64_encode(const std::vector<uint8_t> &data, bool url_safe = false)
    {
        // Standard base64 or URL-safe base64
        static const char *base64_chars_std =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        static const char *base64_chars_url =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
        const char *base64_chars = url_safe ? base64_chars_url : base64_chars_std;

        std::string result;
        int val = 0, valb = -6;
        for (uint8_t c : data)
        {
            val = (val << 8) + c;
            valb += 8;
            while (valb >= 0)
            {
                result.push_back(base64_chars[(val >> valb) & 0x3F]);
                valb -= 6;
            }
        }
        if (valb > -6)
            result.push_back(base64_chars[((val << 8) >> (valb + 8)) & 0x3F]);

        // Add padding
        while (result.size() % 4 != 0)
            result.push_back('=');

        return result;
    }

    OrderSigner::OrderSigner(const std::string &private_key, int chain_id)
        : private_key_(private_key), chain_id_(chain_id)
    {
        secp256k1_ctx_ = secp256k1_context_create(SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY);
        if (!secp256k1_ctx_)
        {
            throw std::runtime_error("Failed to create secp256k1 context");
        }
        address_ = derive_address();
    }

    OrderSigner::~OrderSigner()
    {
        if (secp256k1_ctx_)
        {
            secp256k1_context_destroy(static_cast<secp256k1_context *>(secp256k1_ctx_));
        }
    }

    std::string OrderSigner::derive_address()
    {
        auto ctx = static_cast<secp256k1_context *>(secp256k1_ctx_);
        auto pk_bytes = from_hex(private_key_);
        if (pk_bytes.size() != 32)
        {
            throw std::runtime_error("Invalid private key length");
        }
        secp256k1_pubkey pubkey;
        if (!secp256k1_ec_pubkey_create(ctx, &pubkey, pk_bytes.data()))
        {
            throw std::runtime_error("Failed to create public key");
        }
        uint8_t pubkey_serialized[65];
        size_t pubkey_len = 65;
        secp256k1_ec_pubkey_serialize(ctx, pubkey_serialized, &pubkey_len, &pubkey, SECP256K1_EC_UNCOMPRESSED);
        std::vector<uint8_t> pubkey_data(pubkey_serialized + 1, pubkey_serialized + 65);
        auto hash = keccak256(pubkey_data);

        // Build lowercase address first
        std::stringstream ss_lower;
        for (int i = 12; i < 32; i++)
        {
            ss_lower << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(hash[i]);
        }
        std::string addr_lower = ss_lower.str();

        // EIP-55 checksum: hash the lowercase address and use it to determine case
        auto addr_hash = keccak256(addr_lower);

        std::stringstream ss;
        ss << "0x";
        for (size_t i = 0; i < 40; i++)
        {
            char c = addr_lower[i];
            if (c >= 'a' && c <= 'f')
            {
                // Get the corresponding nibble from the hash
                int hash_nibble = (addr_hash[i / 2] >> (i % 2 == 0 ? 4 : 0)) & 0x0F;
                if (hash_nibble >= 8)
                {
                    c = std::toupper(c);
                }
            }
            ss << c;
        }
        return ss.str();
    }

    std::string OrderSigner::sign_hash(const std::array<uint8_t, 32> &hash)
    {
        auto ctx = static_cast<secp256k1_context *>(secp256k1_ctx_);
        auto pk_bytes = from_hex(private_key_);
        secp256k1_ecdsa_recoverable_signature sig;
        if (!secp256k1_ecdsa_sign_recoverable(ctx, &sig, hash.data(), pk_bytes.data(), nullptr, nullptr))
        {
            throw std::runtime_error("Failed to sign");
        }
        uint8_t sig_serialized[64];
        int recid;
        secp256k1_ecdsa_recoverable_signature_serialize_compact(ctx, sig_serialized, &recid, &sig);
        std::vector<uint8_t> signature(65);
        std::memcpy(signature.data(), sig_serialized, 64);
        signature[64] = static_cast<uint8_t>(recid + 27);
        return to_hex(signature);
    }

    std::array<uint8_t, 32> OrderSigner::hash_domain(const std::string &name, const std::string &version,
                                                     int chain_id, const std::string &verifying_contract)
    {
        auto type_hash = keccak256(std::string("EIP712Domain(string name,string version,uint256 chainId,address verifyingContract)"));
        auto name_hash = keccak256(name);
        auto version_hash = keccak256(version);
        std::vector<uint8_t> chain_id_bytes(32, 0);
        for (int i = 0; i < 4; i++)
        {
            chain_id_bytes[31 - i] = (chain_id >> (i * 8)) & 0xFF;
        }
        auto contract_bytes = from_hex(verifying_contract);
        std::vector<uint8_t> contract_padded(32, 0);
        std::memcpy(contract_padded.data() + 12, contract_bytes.data(), 20);
        std::vector<uint8_t> encoded;
        encoded.insert(encoded.end(), type_hash.begin(), type_hash.end());
        encoded.insert(encoded.end(), name_hash.begin(), name_hash.end());
        encoded.insert(encoded.end(), version_hash.begin(), version_hash.end());
        encoded.insert(encoded.end(), chain_id_bytes.begin(), chain_id_bytes.end());
        encoded.insert(encoded.end(), contract_padded.begin(), contract_padded.end());
        return keccak256(encoded);
    }

    std::array<uint8_t, 32> OrderSigner::hash_order(const OrderData &order, const std::string &salt)
    {
        auto type_hash = keccak256(std::string(
            "Order(uint256 salt,address maker,address signer,address taker,uint256 tokenId,"
            "uint256 makerAmount,uint256 takerAmount,uint256 expiration,uint256 nonce,"
            "uint256 feeRateBps,uint8 side,uint8 signatureType)"));

        auto encode_uint256 = [](const std::string &value) -> std::vector<uint8_t>
        {
            std::vector<uint8_t> result(32, 0);
            if (value.empty())
            {
                return result;
            }
            if (value.size() >= 2 && value.substr(0, 2) == "0x")
            {
                // Hex string
                auto bytes = from_hex(value);
                size_t offset = 32 - std::min(bytes.size(), size_t(32));
                std::memcpy(result.data() + offset, bytes.data(), std::min(bytes.size(), size_t(32)));
            }
            else if (value.size() <= 18)
            {
                // Small enough for uint64_t
                uint64_t val = std::stoull(value);
                for (int i = 0; i < 8; i++)
                {
                    result[31 - i] = (val >> (i * 8)) & 0xFF;
                }
            }
            else
            {
                // Large decimal - convert to bytes manually using repeated division
                std::string num = value;
                std::vector<uint8_t> bytes;
                while (!num.empty() && num != "0")
                {
                    // Divide by 256 and get remainder
                    int remainder = 0;
                    std::string quotient;
                    for (char c : num)
                    {
                        int digit = remainder * 10 + (c - '0');
                        if (!quotient.empty() || digit / 256 > 0)
                        {
                            quotient += ('0' + digit / 256);
                        }
                        remainder = digit % 256;
                    }
                    bytes.push_back(static_cast<uint8_t>(remainder));
                    num = quotient.empty() ? "0" : quotient;
                }
                // Copy bytes in reverse (big-endian)
                size_t offset = 32 - std::min(bytes.size(), size_t(32));
                for (size_t i = 0; i < std::min(bytes.size(), size_t(32)); i++)
                {
                    result[31 - i] = bytes[i];
                }
            }
            return result;
        };

        auto encode_address = [](const std::string &addr) -> std::vector<uint8_t>
        {
            auto bytes = from_hex(addr);
            std::vector<uint8_t> result(32, 0);
            std::memcpy(result.data() + 12, bytes.data(), std::min(bytes.size(), size_t(20)));
            return result;
        };

        std::vector<uint8_t> encoded;
        encoded.insert(encoded.end(), type_hash.begin(), type_hash.end());

        auto salt_enc = encode_uint256(salt);
        encoded.insert(encoded.end(), salt_enc.begin(), salt_enc.end());

        auto maker_enc = encode_address(order.maker);
        encoded.insert(encoded.end(), maker_enc.begin(), maker_enc.end());

        auto signer_enc = encode_address(order.signer);
        encoded.insert(encoded.end(), signer_enc.begin(), signer_enc.end());

        auto taker_enc = encode_address(order.taker);
        encoded.insert(encoded.end(), taker_enc.begin(), taker_enc.end());

        auto token_enc = encode_uint256(order.token_id);
        encoded.insert(encoded.end(), token_enc.begin(), token_enc.end());

        auto maker_amt_enc = encode_uint256(order.maker_amount);
        encoded.insert(encoded.end(), maker_amt_enc.begin(), maker_amt_enc.end());

        auto taker_amt_enc = encode_uint256(order.taker_amount);
        encoded.insert(encoded.end(), taker_amt_enc.begin(), taker_amt_enc.end());

        auto exp_enc = encode_uint256(order.expiration);
        encoded.insert(encoded.end(), exp_enc.begin(), exp_enc.end());

        auto nonce_enc = encode_uint256(order.nonce);
        encoded.insert(encoded.end(), nonce_enc.begin(), nonce_enc.end());

        auto fee_enc = encode_uint256(order.fee_rate_bps);
        encoded.insert(encoded.end(), fee_enc.begin(), fee_enc.end());

        std::vector<uint8_t> side_enc(32, 0);
        side_enc[31] = static_cast<uint8_t>(order.side);
        encoded.insert(encoded.end(), side_enc.begin(), side_enc.end());

        std::vector<uint8_t> sig_type_enc(32, 0);
        sig_type_enc[31] = static_cast<uint8_t>(order.signature_type);
        encoded.insert(encoded.end(), sig_type_enc.begin(), sig_type_enc.end());

        return keccak256(encoded);
    }

    std::array<uint8_t, 32> OrderSigner::encode_eip712(const std::array<uint8_t, 32> &domain_hash,
                                                       const std::array<uint8_t, 32> &struct_hash)
    {
        std::vector<uint8_t> encoded;
        encoded.push_back(0x19);
        encoded.push_back(0x01);
        encoded.insert(encoded.end(), domain_hash.begin(), domain_hash.end());
        encoded.insert(encoded.end(), struct_hash.begin(), struct_hash.end());
        return keccak256(encoded);
    }

    SignedOrder OrderSigner::sign_order(const OrderData &order, const std::string &exchange_address)
    {
        std::string salt = generate_salt();
        return sign_order_with_salt(order, exchange_address, salt);
    }

    SignedOrder OrderSigner::sign_order_with_salt(const OrderData &order, const std::string &exchange_address, const std::string &salt)
    {
        auto domain_hash = hash_domain("Polymarket CTF Exchange", "1", chain_id_, exchange_address);
        auto order_hash = hash_order(order, salt);
        auto message_hash = encode_eip712(domain_hash, order_hash);

        std::string signature = sign_hash(message_hash);

        SignedOrder signed_order;
        signed_order.salt = salt;
        signed_order.maker = order.maker;
        signed_order.signer = order.signer;
        signed_order.taker = order.taker;
        signed_order.token_id = order.token_id;
        signed_order.maker_amount = order.maker_amount;
        signed_order.taker_amount = order.taker_amount;
        signed_order.expiration = order.expiration;
        signed_order.nonce = order.nonce;
        signed_order.fee_rate_bps = order.fee_rate_bps;
        signed_order.side = static_cast<int>(order.side);
        signed_order.signature_type = static_cast<int>(order.signature_type);
        signed_order.signature = signature;

        return signed_order;
    }

    std::array<uint8_t, 32> OrderSigner::hash_clob_auth_domain()
    {
        auto type_hash = keccak256(std::string("EIP712Domain(string name,string version,uint256 chainId)"));
        auto name_hash = keccak256(std::string("ClobAuthDomain"));
        auto version_hash = keccak256(std::string("1"));

        std::vector<uint8_t> chain_id_bytes(32, 0);
        for (int i = 0; i < 4; i++)
        {
            chain_id_bytes[31 - i] = (chain_id_ >> (i * 8)) & 0xFF;
        }

        std::vector<uint8_t> encoded;
        encoded.insert(encoded.end(), type_hash.begin(), type_hash.end());
        encoded.insert(encoded.end(), name_hash.begin(), name_hash.end());
        encoded.insert(encoded.end(), version_hash.begin(), version_hash.end());
        encoded.insert(encoded.end(), chain_id_bytes.begin(), chain_id_bytes.end());

        return keccak256(encoded);
    }

    std::array<uint8_t, 32> OrderSigner::hash_clob_auth(const std::string &timestamp, uint64_t nonce)
    {
        auto type_hash = keccak256(std::string(
            "ClobAuth(address address,string timestamp,uint256 nonce,string message)"));

        // Encode address (padded to 32 bytes)
        auto addr_bytes = from_hex(address_);
        std::vector<uint8_t> addr_padded(32, 0);
        std::memcpy(addr_padded.data() + 12, addr_bytes.data(), std::min(addr_bytes.size(), size_t(20)));

        // Hash the timestamp string
        auto timestamp_hash = keccak256(timestamp);

        // Encode nonce as uint256
        std::vector<uint8_t> nonce_bytes(32, 0);
        for (int i = 0; i < 8; i++)
        {
            nonce_bytes[31 - i] = (nonce >> (i * 8)) & 0xFF;
        }

        // Hash the message string
        auto message_hash = keccak256(std::string("This message attests that I control the given wallet"));

        std::vector<uint8_t> encoded;
        encoded.insert(encoded.end(), type_hash.begin(), type_hash.end());
        encoded.insert(encoded.end(), addr_padded.begin(), addr_padded.end());
        encoded.insert(encoded.end(), timestamp_hash.begin(), timestamp_hash.end());
        encoded.insert(encoded.end(), nonce_bytes.begin(), nonce_bytes.end());
        encoded.insert(encoded.end(), message_hash.begin(), message_hash.end());

        return keccak256(encoded);
    }

    OrderSigner::L1Headers OrderSigner::generate_l1_headers(uint64_t nonce, const std::string &override_address)
    {
        auto now = std::chrono::system_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
        std::string ts_str = std::to_string(timestamp);

        auto domain_hash = hash_clob_auth_domain();
        auto struct_hash = hash_clob_auth(ts_str, nonce);
        auto message_hash = encode_eip712(domain_hash, struct_hash);

        std::string signature = sign_hash(message_hash);

        L1Headers headers;
        // Always use signer address for L1 auth (even for proxy wallets)
        // The override_address parameter is ignored for L1 - it's only for reference
        headers.poly_address = address_;
        headers.poly_signature = signature;
        headers.poly_timestamp = ts_str;
        headers.poly_nonce = std::to_string(nonce);

        return headers;
    }

    ApiCredentials OrderSigner::derive_api_credentials(HttpClient &http, const std::string &funder_address)
    {
        auto headers = generate_l1_headers(0, funder_address);

        std::map<std::string, std::string> req_headers;
        req_headers["POLY_ADDRESS"] = headers.poly_address;
        req_headers["POLY_SIGNATURE"] = headers.poly_signature;
        req_headers["POLY_TIMESTAMP"] = headers.poly_timestamp;
        req_headers["POLY_NONCE"] = headers.poly_nonce;

        auto response = http.get("/auth/derive-api-key", req_headers);

        if (!response.ok())
        {
            throw std::runtime_error("Failed to derive API key: " + response.body);
        }

        auto j = json::parse(response.body);

        ApiCredentials creds;
        creds.api_key = j["apiKey"].get<std::string>();
        creds.api_secret = j["secret"].get<std::string>();
        creds.api_passphrase = j["passphrase"].get<std::string>();

        return creds;
    }

    ApiCredentials OrderSigner::create_api_credentials(HttpClient &http, uint64_t nonce, const std::string &funder_address)
    {
        auto headers = generate_l1_headers(nonce, funder_address);

        std::map<std::string, std::string> req_headers;
        req_headers["POLY_ADDRESS"] = headers.poly_address;
        req_headers["POLY_SIGNATURE"] = headers.poly_signature;
        req_headers["POLY_TIMESTAMP"] = headers.poly_timestamp;
        req_headers["POLY_NONCE"] = headers.poly_nonce;

        auto response = http.post("/auth/api-key", "{}", req_headers);

        if (!response.ok())
        {
            throw std::runtime_error("Failed to create API key: " + response.body);
        }

        auto j = json::parse(response.body);

        ApiCredentials creds;
        creds.api_key = j["apiKey"].get<std::string>();
        creds.api_secret = j["secret"].get<std::string>();
        creds.api_passphrase = j["passphrase"].get<std::string>();

        return creds;
    }

    ApiCredentials OrderSigner::create_or_derive_api_credentials(HttpClient &http, const std::string &funder_address)
    {
        // Try to derive first (existing key)
        try
        {
            return derive_api_credentials(http, funder_address);
        }
        catch (...)
        {
            // If derive fails, try to create
            try
            {
                return create_api_credentials(http, 0, funder_address);
            }
            catch (...)
            {
                // Some setups may still work without explicit API keys
                throw std::runtime_error("Could not derive or create API credentials");
            }
        }
    }

    OrderSigner::L2Headers OrderSigner::generate_l2_headers(const ApiCredentials &creds,
                                                            const std::string &method,
                                                            const std::string &path,
                                                            const std::string &body,
                                                            const std::string &funder_address)
    {
        auto now = std::chrono::system_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();

        std::string message = std::to_string(timestamp) + method + path;
        if (!body.empty())
        {
            message += body;
        }

        auto secret_bytes = base64_decode(creds.api_secret);

        unsigned char hmac_result[32];
        unsigned int hmac_len = 32;

        HMAC(EVP_sha256(),
             secret_bytes.data(), secret_bytes.size(),
             reinterpret_cast<const unsigned char *>(message.c_str()), message.size(),
             hmac_result, &hmac_len);

        std::vector<uint8_t> hmac_vec(hmac_result, hmac_result + 32);
        // L2 HMAC signature must be URL-safe base64 (- and _ instead of + and /)
        std::string signature = base64_encode(hmac_vec, true);

        L2Headers headers;
        // Always use signer address for L2 auth - the API key is associated with the signer
        headers.poly_address = address_;
        headers.poly_timestamp = std::to_string(timestamp);
        headers.poly_api_key = creds.api_key;
        headers.poly_passphrase = creds.api_passphrase;
        headers.poly_secret = creds.api_secret;
        headers.poly_signature = signature;

        return headers;
    }

} // namespace polymarket
