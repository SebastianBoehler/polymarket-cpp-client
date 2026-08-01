#include "order_signer.hpp"
#include "decimal_math.hpp"
#include <secp256k1.h>
#include <secp256k1_recovery.h>
#include <ethash/keccak.hpp>
#include <openssl/crypto.h>
#include <algorithm>
#include <random>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <stdexcept>
#include <string_view>
#include <utility>

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
        std::string_view h = hex;
        if (h.substr(0, 2) == "0x" || h.substr(0, 2) == "0X")
        {
            h.remove_prefix(2);
        }
        if (h.size() % 2 != 0)
            throw std::invalid_argument("hex input must contain complete bytes");

        const auto nibble = [](char character) -> uint8_t
        {
            if (character >= '0' && character <= '9')
                return static_cast<uint8_t>(character - '0');
            if (character >= 'a' && character <= 'f')
                return static_cast<uint8_t>(character - 'a' + 10);
            if (character >= 'A' && character <= 'F')
                return static_cast<uint8_t>(character - 'A' + 10);
            throw std::invalid_argument("hex input contains a non-hexadecimal character");
        };

        std::vector<uint8_t> result;
        result.reserve(h.size() / 2);
        for (size_t i = 0; i < h.size(); i += 2)
        {
            result.push_back(static_cast<uint8_t>((nibble(h[i]) << 4) | nibble(h[i + 1])));
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
        return decimal_to_scaled_integer(amount,
                                         decimals,
                                         round_down ? DecimalRoundingMode::Down : DecimalRoundingMode::Nearest);
    }

    std::string generate_salt()
    {
        thread_local std::mt19937_64 gen(std::random_device{}());
        static constexpr uint64_t kMaxSignedSalt = 0x7fffffffffffffffULL;
        std::uniform_int_distribution<uint64_t> dis(0, kMaxSignedSalt);
        return std::to_string(dis(gen));
    }

    void OrderSigner::SecurePrivateKey::clear() noexcept
    {
        OPENSSL_cleanse(bytes.data(), bytes.size());
    }

    OrderSigner::SecurePrivateKey::~SecurePrivateKey()
    {
        clear();
    }

    OrderSigner::SecurePrivateKey::SecurePrivateKey(SecurePrivateKey &&other) noexcept
        : bytes(other.bytes)
    {
        other.clear();
    }

    OrderSigner::SecurePrivateKey &OrderSigner::SecurePrivateKey::operator=(SecurePrivateKey &&other) noexcept
    {
        if (this != &other)
        {
            clear();
            bytes = other.bytes;
            other.clear();
        }
        return *this;
    }

    void OrderSigner::Secp256k1ContextDeleter::operator()(void *context) const noexcept
    {
        if (context)
            secp256k1_context_destroy(static_cast<secp256k1_context *>(context));
    }

    OrderSigner::OrderSigner(const std::string &private_key, int chain_id)
        : chain_id_(chain_id)
    {
        auto private_key_bytes = from_hex(private_key);
        if (private_key_bytes.size() != private_key_.bytes.size())
        {
            if (!private_key_bytes.empty())
                OPENSSL_cleanse(private_key_bytes.data(), private_key_bytes.size());
            throw std::runtime_error("Invalid private key length");
        }
        std::copy(private_key_bytes.begin(), private_key_bytes.end(), private_key_.bytes.begin());
        OPENSSL_cleanse(private_key_bytes.data(), private_key_bytes.size());

        secp256k1_ctx_.reset(
            secp256k1_context_create(SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY));
        if (!secp256k1_ctx_)
        {
            throw std::runtime_error("Failed to create secp256k1 context");
        }
        address_ = derive_address();
    }

    OrderSigner::~OrderSigner()
    {
        private_key_.clear();
    }

    OrderSigner::OrderSigner(OrderSigner &&other)
        : private_key_(std::move(other.private_key_)),
          address_(std::move(other.address_)),
          chain_id_(std::exchange(other.chain_id_, 0)),
          secp256k1_ctx_(std::move(other.secp256k1_ctx_))
    {
        std::lock_guard<std::mutex> lock(other.domain_cache_mutex_);
        domain_cache_ = std::move(other.domain_cache_);
    }

    OrderSigner &OrderSigner::operator=(OrderSigner &&other)
    {
        if (this != &other)
        {
            std::scoped_lock lock(domain_cache_mutex_, other.domain_cache_mutex_);
            private_key_ = std::move(other.private_key_);
            address_ = std::move(other.address_);
            chain_id_ = std::exchange(other.chain_id_, 0);
            secp256k1_ctx_ = std::move(other.secp256k1_ctx_);
            domain_cache_ = std::move(other.domain_cache_);
        }
        return *this;
    }

    std::string OrderSigner::derive_address()
    {
        auto ctx = static_cast<secp256k1_context *>(secp256k1_ctx_.get());
        secp256k1_pubkey pubkey;
        if (!secp256k1_ec_pubkey_create(ctx, &pubkey, private_key_.bytes.data()))
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
        auto ctx = static_cast<secp256k1_context *>(secp256k1_ctx_.get());
        secp256k1_ecdsa_recoverable_signature sig;
        if (!secp256k1_ecdsa_sign_recoverable(ctx, &sig, hash.data(), private_key_.bytes.data(), nullptr, nullptr))
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

} // namespace polymarket
