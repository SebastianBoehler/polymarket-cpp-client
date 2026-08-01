#include "order_signer.hpp"

#include <cstring>
#include <stdexcept>
#include <vector>

namespace polymarket
{
    std::array<uint8_t, 32> OrderSigner::hash_domain(const std::string &name,
                                                     const std::string &version,
                                                     int chain_id,
                                                     const std::string &verifying_contract)
    {
        const auto type_hash = keccak256(
            std::string("EIP712Domain(string name,string version,uint256 chainId,address verifyingContract)"));
        const auto name_hash = keccak256(name);
        const auto version_hash = keccak256(version);
        std::vector<uint8_t> chain_id_bytes(32, 0);
        for (int i = 0; i < 4; ++i)
        {
            chain_id_bytes[31 - i] = (chain_id >> (i * 8)) & 0xFF;
        }
        const auto contract_bytes = from_hex(verifying_contract);
        if (contract_bytes.size() != 20)
        {
            throw std::invalid_argument("verifying contract must be a 20-byte address");
        }
        std::vector<uint8_t> contract_padded(32, 0);
        std::memcpy(contract_padded.data() + 12, contract_bytes.data(), 20);

        std::vector<uint8_t> encoded;
        encoded.reserve(160);
        encoded.insert(encoded.end(), type_hash.begin(), type_hash.end());
        encoded.insert(encoded.end(), name_hash.begin(), name_hash.end());
        encoded.insert(encoded.end(), version_hash.begin(), version_hash.end());
        encoded.insert(encoded.end(), chain_id_bytes.begin(), chain_id_bytes.end());
        encoded.insert(encoded.end(), contract_padded.begin(), contract_padded.end());
        return keccak256(encoded);
    }

    std::array<uint8_t, 32> OrderSigner::v2_domain_hash(const std::string &exchange_address)
    {
        std::lock_guard<std::mutex> lock(domain_cache_mutex_);
        auto [entry, inserted] = domain_cache_.try_emplace(exchange_address);
        if (inserted)
        {
            entry->second = hash_domain("Polymarket CTF Exchange", "2", chain_id_, exchange_address);
        }
        return entry->second;
    }

    std::array<uint8_t, 32> OrderSigner::encode_eip712(
        const std::array<uint8_t, 32> &domain_hash,
        const std::array<uint8_t, 32> &struct_hash)
    {
        std::vector<uint8_t> encoded;
        encoded.reserve(66);
        encoded.push_back(0x19);
        encoded.push_back(0x01);
        encoded.insert(encoded.end(), domain_hash.begin(), domain_hash.end());
        encoded.insert(encoded.end(), struct_hash.begin(), struct_hash.end());
        return keccak256(encoded);
    }
} // namespace polymarket
