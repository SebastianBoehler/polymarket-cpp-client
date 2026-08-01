#include "order_signer.hpp"
#include "http_client.hpp"
#include "order_signer_auth_internal.hpp"

#include <chrono>
#include <cstring>
#include <nlohmann/json.hpp>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <stdexcept>
#include <vector>

namespace polymarket
{
    namespace
    {
        int base64_value(unsigned char character)
        {
            if (character >= 'A' && character <= 'Z')
                return character - 'A';
            if (character >= 'a' && character <= 'z')
                return character - 'a' + 26;
            if (character >= '0' && character <= '9')
                return character - '0' + 52;
            if (character == '+' || character == '-')
                return 62;
            if (character == '/' || character == '_')
                return 63;
            return -1;
        }

        std::vector<uint8_t> base64_decode(const std::string &encoded)
        {
            std::vector<uint8_t> result;
            result.reserve(encoded.size() * 3 / 4);
            uint32_t value = 0;
            int bits = 0;
            std::size_t data_characters = 0;
            std::size_t padding = 0;
            bool saw_padding = false;
            for (const unsigned char character : encoded)
            {
                if (character == '=')
                {
                    saw_padding = true;
                    if (++padding > 2)
                        throw std::invalid_argument("API secret is not valid base64");
                    continue;
                }
                const int digit = base64_value(character);
                if (saw_padding || digit < 0)
                    throw std::invalid_argument("API secret is not valid base64");
                value = (value << 6) | static_cast<uint32_t>(digit);
                bits += 6;
                ++data_characters;
                if (bits >= 8)
                {
                    bits -= 8;
                    result.push_back(static_cast<uint8_t>((value >> bits) & 0xFF));
                    value = bits == 0 ? 0 : value & ((uint32_t{1} << bits) - 1);
                }
            }
            const auto remainder = data_characters % 4;
            const auto expected_padding = remainder == 0 ? 0 : 4 - remainder;
            if (remainder == 1 ||
                (padding != 0 && (padding != expected_padding ||
                                  (data_characters + padding) % 4 != 0)) ||
                value != 0)
            {
                throw std::invalid_argument("API secret is not valid base64");
            }
            return result;
        }

        std::string base64_url_encode(const std::vector<uint8_t> &data)
        {
            static constexpr char alphabet[] =
                "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
            std::string result;
            result.reserve((data.size() + 2) / 3 * 4);
            uint32_t value = 0;
            int bits = 0;
            for (const uint8_t character : data)
            {
                value = (value << 8) | character;
                bits += 8;
                while (bits >= 6)
                {
                    bits -= 6;
                    result.push_back(alphabet[(value >> bits) & 0x3F]);
                    value = bits == 0 ? 0 : value & ((uint32_t{1} << bits) - 1);
                }
            }
            if (bits > 0)
            {
                result.push_back(alphabet[(value << (6 - bits)) & 0x3F]);
            }
            while (result.size() % 4 != 0)
            {
                result.push_back('=');
            }
            return result;
        }

        std::map<std::string, std::string> l1_request_headers(const OrderSigner::L1Headers &headers)
        {
            return {{"POLY_ADDRESS", headers.poly_address},
                    {"POLY_SIGNATURE", headers.poly_signature},
                    {"POLY_TIMESTAMP", headers.poly_timestamp},
                    {"POLY_NONCE", headers.poly_nonce}};
        }

        ApiCredentials parse_credentials(const std::string &body)
        {
            const auto parsed = nlohmann::json::parse(body);
            ApiCredentials credentials{parsed.at("apiKey").get<std::string>(),
                                       parsed.at("secret").get<std::string>(),
                                       parsed.at("passphrase").get<std::string>()};
            detail::validate_api_credentials(credentials);
            return credentials;
        }
    } // namespace

    void detail::validate_api_credentials(const ApiCredentials &credentials)
    {
        if (credentials.api_key.empty() || credentials.api_secret.empty() ||
            credentials.api_passphrase.empty() ||
            base64_decode(credentials.api_secret).empty())
        {
            throw std::invalid_argument(
                "API key, secret, and passphrase must be non-empty and valid");
        }
    }

    std::array<uint8_t, 32> OrderSigner::hash_clob_auth_domain()
    {
        const auto type_hash = keccak256(std::string("EIP712Domain(string name,string version,uint256 chainId)"));
        const auto name_hash = keccak256(std::string("ClobAuthDomain"));
        const auto version_hash = keccak256(std::string("1"));
        std::vector<uint8_t> chain_id_bytes(32, 0);
        for (int i = 0; i < 4; ++i)
        {
            chain_id_bytes[31 - i] = (chain_id_ >> (i * 8)) & 0xFF;
        }

        std::vector<uint8_t> encoded;
        encoded.reserve(128);
        encoded.insert(encoded.end(), type_hash.begin(), type_hash.end());
        encoded.insert(encoded.end(), name_hash.begin(), name_hash.end());
        encoded.insert(encoded.end(), version_hash.begin(), version_hash.end());
        encoded.insert(encoded.end(), chain_id_bytes.begin(), chain_id_bytes.end());
        return keccak256(encoded);
    }

    std::array<uint8_t, 32> OrderSigner::hash_clob_auth(const std::string &address,
                                                        const std::string &timestamp,
                                                        uint64_t nonce)
    {
        const auto type_hash = keccak256(std::string(
            "ClobAuth(address address,string timestamp,uint256 nonce,string message)"));
        const auto address_bytes = from_hex(address);
        if (address_bytes.size() != 20)
        {
            throw std::invalid_argument("authentication address must be a 20-byte address");
        }
        std::vector<uint8_t> address_padded(32, 0);
        std::memcpy(address_padded.data() + 12, address_bytes.data(), 20);
        const auto timestamp_hash = keccak256(timestamp);

        std::vector<uint8_t> nonce_bytes(32, 0);
        for (int i = 0; i < 8; ++i)
        {
            nonce_bytes[31 - i] = (nonce >> (i * 8)) & 0xFF;
        }
        const auto message_hash = keccak256(
            std::string("This message attests that I control the given wallet"));

        std::vector<uint8_t> encoded;
        encoded.reserve(160);
        encoded.insert(encoded.end(), type_hash.begin(), type_hash.end());
        encoded.insert(encoded.end(), address_padded.begin(), address_padded.end());
        encoded.insert(encoded.end(), timestamp_hash.begin(), timestamp_hash.end());
        encoded.insert(encoded.end(), nonce_bytes.begin(), nonce_bytes.end());
        encoded.insert(encoded.end(), message_hash.begin(), message_hash.end());
        return keccak256(encoded);
    }

    OrderSigner::L1Headers OrderSigner::generate_l1_headers(uint64_t nonce,
                                                            const std::string &)
    {
        const auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                                   std::chrono::system_clock::now().time_since_epoch())
                                   .count();
        const std::string timestamp_text = std::to_string(timestamp);
        const auto message_hash = encode_eip712(
            hash_clob_auth_domain(), hash_clob_auth(address_, timestamp_text, nonce));
        return {address_, sign_hash(message_hash), timestamp_text, std::to_string(nonce)};
    }

    ApiCredentials OrderSigner::derive_api_credentials(HttpClient &http,
                                                        const std::string &)
    {
        const auto headers = generate_l1_headers();
        const auto response = http.get("/auth/derive-api-key", l1_request_headers(headers));
        if (!response.ok())
        {
            throw std::runtime_error("Failed to derive API key: " + response.body);
        }
        return parse_credentials(response.body);
    }

    ApiCredentials OrderSigner::create_api_credentials(HttpClient &http, uint64_t nonce,
                                                        const std::string &)
    {
        const auto headers = generate_l1_headers(nonce);
        const auto response = http.post("/auth/api-key", "{}", l1_request_headers(headers));
        if (!response.ok())
        {
            throw std::runtime_error("Failed to create API key: " + response.body);
        }
        return parse_credentials(response.body);
    }

    ApiCredentials OrderSigner::create_or_derive_api_credentials(HttpClient &http,
                                                                  const std::string &)
    {
        try
        {
            return derive_api_credentials(http);
        }
        catch (const std::exception &derive_error)
        {
            try
            {
                return create_api_credentials(http);
            }
            catch (const std::exception &create_error)
            {
                throw std::runtime_error(std::string("Could not derive or create API credentials: derive: ") +
                                         derive_error.what() + "; create: " + create_error.what());
            }
        }
    }

    OrderSigner::L2Headers OrderSigner::generate_l2_headers(
        const ApiCredentials &credentials, const std::string &method,
        const std::string &path, const std::string &body, const std::string &)
    {
        detail::validate_api_credentials(credentials);
        const auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                                   std::chrono::system_clock::now().time_since_epoch())
                                   .count();
        std::string message = std::to_string(timestamp) + method + path + body;
        const auto secret = base64_decode(credentials.api_secret);

        unsigned char digest[EVP_MAX_MD_SIZE];
        unsigned int digest_size = 0;
        if (!HMAC(EVP_sha256(), secret.data(), static_cast<int>(secret.size()),
                  reinterpret_cast<const unsigned char *>(message.data()), message.size(),
                  digest, &digest_size))
        {
            throw std::runtime_error("failed to generate L2 authentication signature");
        }
        const std::vector<uint8_t> signature_bytes(digest, digest + digest_size);
        return {address_, base64_url_encode(signature_bytes), std::to_string(timestamp),
                credentials.api_key, credentials.api_passphrase, credentials.api_secret};
    }
} // namespace polymarket
