#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <array>

namespace polymarket
{
    // Forward declaration
    class HttpClient;

    // Signature types supported by Polymarket
    enum class SignatureType
    {
        EOA = 0,             // Externally Owned Account (standard wallet)
        POLY_PROXY = 1,      // Polymarket proxy wallet
        POLY_GNOSIS_SAFE = 2 // Gnosis Safe (used for email wallets)
    };

    // Order side
    enum class OrderSide
    {
        BUY = 0,
        SELL = 1
    };

    // Order data structure matching Polymarket's format
    struct OrderData
    {
        std::string maker;        // Address that holds funds
        std::string taker;        // Usually 0x0000...0000
        std::string token_id;     // Token to trade
        std::string maker_amount; // Amount in wei (6 decimals for USDC)
        std::string taker_amount; // Amount in wei
        OrderSide side;
        std::string fee_rate_bps; // Fee in basis points (usually "0")
        std::string nonce;        // Order nonce
        std::string signer;       // Address of the signer
        std::string expiration;   // Unix timestamp or "0" for no expiration
        SignatureType signature_type;
    };

    // Signed order ready to be posted
    struct SignedOrder
    {
        std::string salt;
        std::string maker;
        std::string signer;
        std::string taker;
        std::string token_id;
        std::string maker_amount;
        std::string taker_amount;
        std::string expiration;
        std::string nonce;
        std::string fee_rate_bps;
        int side;
        int signature_type;
        std::string signature;
    };

    // API credentials for L2 authentication
    struct ApiCredentials
    {
        std::string api_key;
        std::string api_secret;
        std::string api_passphrase;
    };

    class OrderSigner
    {
    public:
        OrderSigner(const std::string &private_key, int chain_id = 137);
        ~OrderSigner();

        // Get the signer's address
        std::string address() const { return address_; }

        // Create and sign an order
        SignedOrder sign_order(const OrderData &order, const std::string &exchange_address);
        SignedOrder sign_order_with_salt(const OrderData &order, const std::string &exchange_address, const std::string &salt);

        // Sign a message hash (returns 65-byte signature with v,r,s)
        std::string sign_hash(const std::array<uint8_t, 32> &hash);

        // Create or derive API credentials (L1 authentication)
        // This signs an EIP-712 ClobAuthDomain message to authenticate with the API
        struct L1Headers
        {
            std::string poly_address;
            std::string poly_signature;
            std::string poly_timestamp;
            std::string poly_nonce;
        };
        L1Headers generate_l1_headers(uint64_t nonce = 0, const std::string &override_address = "");

        // Derive existing API credentials from the server
        ApiCredentials derive_api_credentials(HttpClient &http, const std::string &funder_address = "");

        // Create new API credentials on the server
        ApiCredentials create_api_credentials(HttpClient &http, uint64_t nonce = 0, const std::string &funder_address = "");

        // Create or derive (tries derive first, then create)
        ApiCredentials create_or_derive_api_credentials(HttpClient &http, const std::string &funder_address = "");

        // Generate L2 authentication headers
        struct L2Headers
        {
            std::string poly_address;
            std::string poly_signature;
            std::string poly_timestamp;
            std::string poly_api_key;
            std::string poly_passphrase;
            std::string poly_secret;
        };
        L2Headers generate_l2_headers(const ApiCredentials &creds, const std::string &method,
                                      const std::string &path, const std::string &body = "",
                                      const std::string &funder_address = "");

    private:
        std::string private_key_;
        std::string address_;
        int chain_id_;
        void *secp256k1_ctx_; // secp256k1_context*

        // Derive address from private key
        std::string derive_address();

        // EIP-712 encoding helpers
        std::array<uint8_t, 32> hash_domain(const std::string &name, const std::string &version,
                                            int chain_id, const std::string &verifying_contract);
        std::array<uint8_t, 32> hash_order(const OrderData &order, const std::string &salt);
        std::array<uint8_t, 32> encode_eip712(const std::array<uint8_t, 32> &domain_hash,
                                              const std::array<uint8_t, 32> &struct_hash);

        // L1 auth helpers
        std::array<uint8_t, 32> hash_clob_auth_domain();
        std::array<uint8_t, 32> hash_clob_auth(const std::string &timestamp, uint64_t nonce);
    };

    // Utility functions
    std::string to_hex(const std::vector<uint8_t> &data);
    std::string to_hex(const std::array<uint8_t, 32> &data);
    std::vector<uint8_t> from_hex(const std::string &hex);
    std::array<uint8_t, 32> keccak256(const std::vector<uint8_t> &data);
    std::array<uint8_t, 32> keccak256(const std::string &data);

    // Convert USDC amount to wei (6 decimals)
    std::string to_wei(double amount, int decimals = 6, bool round_down = true);

    // Generate random salt
    std::string generate_salt();

} // namespace polymarket
