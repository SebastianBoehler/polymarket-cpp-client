#include "position_calls.hpp"
#include "polymarket/evm_abi.hpp"
#include "evm_uint.hpp"
#include "polymarket/evm_utils.hpp"
#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace polymarket::detail
{
    namespace
    {
        const std::string kZeroBytes32 = "0x" + std::string(64, '0');

        std::vector<EvmAbiValue> binary_partition()
        {
            return {EvmAbiValue::uint256(1), EvmAbiValue::uint256(2)};
        }

        std::vector<uint8_t> uint256_word(const std::string &value, const char *label)
        {
            try
            {
                return parse_uint256_word(value);
            }
            catch (const std::invalid_argument &error)
            {
                throw std::invalid_argument(std::string(label) + ": " + error.what());
            }
        }

        bool is_zero_word(const std::vector<uint8_t> &word)
        {
            return std::all_of(word.begin(), word.end(), [](uint8_t byte) { return byte == 0; });
        }

        bool is_hex_string(const std::string &value, size_t hex_digits)
        {
            if (value.size() != hex_digits + 2 || value.rfind("0x", 0) != 0)
                return false;
            for (size_t i = 2; i < value.size(); ++i)
            {
                if (!std::isxdigit(static_cast<unsigned char>(value[i])))
                    return false;
            }
            return true;
        }

    } // namespace

    std::string normalize_condition_id(const std::string &condition_id)
    {
        if (!is_hex_string(condition_id, 64))
            throw std::invalid_argument("condition_id must be 0x followed by 32 bytes of hex");
        return evm_normalize_hex(condition_id);
    }

    std::string v2_condition_id_bytes31(const std::string &condition_id)
    {
        auto normalized = evm_normalize_hex(condition_id);
        if (normalized.size() == 66 && (normalized.ends_with("00") || normalized.ends_with("01")))
            normalized.resize(64);
        if (!is_hex_string(normalized, 62))
            throw std::invalid_argument(
                "Protocol V2 condition id must be bytes31, or bytes32 ending in an outcome byte");
        return normalized;
    }

    std::string normalize_requested_condition_id(const std::string &condition_id)
    {
        if (!is_hex_string(condition_id, 64) && !is_hex_string(condition_id, 62))
            throw std::invalid_argument(
                "condition_id must be 0x followed by 32 bytes of hex (or 31 bytes for a Protocol V2 market)");
        return evm_normalize_hex(condition_id);
    }

    ContractCall ctf_split_position_call(const std::string &adapter, const std::string &collateral,
                                         const std::string &condition_id, const std::string &amount)
    {
        return {adapter,
                evm_abi_encode_call("splitPosition(address,bytes32,bytes32,uint256[],uint256)",
                                    {EvmAbiValue::address(collateral), EvmAbiValue::bytes32(kZeroBytes32),
                                     EvmAbiValue::bytes32(normalize_condition_id(condition_id)),
                                     EvmAbiValue::array(binary_partition()), EvmAbiValue::uint256(amount)})};
    }

    ContractCall ctf_merge_positions_call(const std::string &adapter, const std::string &collateral,
                                          const std::string &condition_id, const std::string &amount)
    {
        return {adapter,
                evm_abi_encode_call("mergePositions(address,bytes32,bytes32,uint256[],uint256)",
                                    {EvmAbiValue::address(collateral), EvmAbiValue::bytes32(kZeroBytes32),
                                     EvmAbiValue::bytes32(normalize_condition_id(condition_id)),
                                     EvmAbiValue::array(binary_partition()), EvmAbiValue::uint256(amount)})};
    }

    ContractCall ctf_redeem_positions_call(const std::string &adapter, const std::string &collateral,
                                           const std::string &condition_id)
    {
        return {adapter,
                evm_abi_encode_call("redeemPositions(address,bytes32,bytes32,uint256[])",
                                    {EvmAbiValue::address(collateral), EvmAbiValue::bytes32(kZeroBytes32),
                                     EvmAbiValue::bytes32(normalize_condition_id(condition_id)),
                                     EvmAbiValue::array(binary_partition())})};
    }

    ContractCall router_split_call(const std::string &router, const std::string &condition_id,
                                   const std::string &amount)
    {
        return {router, evm_abi_encode_call("split(bytes31,uint256)",
                                            {EvmAbiValue::fixed_bytes(v2_condition_id_bytes31(condition_id), 31),
                                             EvmAbiValue::uint256(amount)})};
    }

    ContractCall router_merge_call(const std::string &router, const std::string &condition_id,
                                   const std::string &amount)
    {
        return {router, evm_abi_encode_call("merge(bytes31,uint256)",
                                            {EvmAbiValue::fixed_bytes(v2_condition_id_bytes31(condition_id), 31),
                                             EvmAbiValue::uint256(amount)})};
    }

    ContractCall router_redeem_call(const std::string &router, const std::string &condition_id,
                                    unsigned outcome_index, const std::string &amount)
    {
        return {router, evm_abi_encode_call("redeem(bytes31,uint256,uint256)",
                                            {EvmAbiValue::fixed_bytes(v2_condition_id_bytes31(condition_id), 31),
                                             EvmAbiValue::uint256(outcome_index), EvmAbiValue::uint256(amount)})};
    }

    ContractCall split_call(const MarketPositionContext &market, const PolymarketContracts &contracts,
                            const std::string &amount)
    {
        if (market.protocol == MarketProtocol::V2)
            return router_split_call(market.operator_contract, market.condition_id, amount);
        return ctf_split_position_call(market.operator_contract, contracts.collateral_token,
                                       market.condition_id, amount);
    }

    ContractCall merge_call(const MarketPositionContext &market, const PolymarketContracts &contracts,
                            const std::string &amount)
    {
        if (market.protocol == MarketProtocol::V2)
            return router_merge_call(market.operator_contract, market.condition_id, amount);
        return ctf_merge_positions_call(market.operator_contract, contracts.collateral_token,
                                        market.condition_id, amount);
    }

    ContractCall balance_of_batch_call(const MarketPositionContext &market, const std::string &owner)
    {
        return {market.position_token_contract,
                evm_abi_encode_call("balanceOfBatch(address[],uint256[])",
                                    {EvmAbiValue::array({EvmAbiValue::address(owner), EvmAbiValue::address(owner)}),
                                     EvmAbiValue::array({EvmAbiValue::uint256(market.token_ids[0]),
                                                         EvmAbiValue::uint256(market.token_ids[1])})})};
    }

    std::array<std::string, 2> decode_binary_balances(const std::string &return_data)
    {
        std::vector<std::string> balances;
        try
        {
            balances = evm_decode_uint_array(return_data, 0);
        }
        catch (const std::invalid_argument &error)
        {
            throw std::runtime_error(std::string("invalid balanceOfBatch result: ") + error.what());
        }
        if (balances.size() != 2)
            throw std::runtime_error("invalid balanceOfBatch result: expected two balances");
        return {balances[0], balances[1]};
    }

    std::string resolve_merge_amount(const std::string &condition_id,
                                     const std::array<std::string, 2> &balances,
                                     const std::string &requested)
    {
        const auto yes = uint256_word(balances[0], "balance");
        const auto no = uint256_word(balances[1], "balance");
        const auto &max_amount = yes < no ? balances[0] : balances[1];
        if (uint256_is_zero(max_amount))
            throw std::invalid_argument("no complementary positions to merge for condition " + condition_id);
        if (requested == "max")
            return max_amount;
        require_positive_amount(requested, "merge amount");
        if (uint256_word(max_amount, "balance") < uint256_word(requested, "merge amount"))
            throw std::invalid_argument("merge amount " + requested + " exceeds the mergeable " + max_amount +
                                        " for condition " + condition_id);
        return requested;
    }

    void require_positive_amount(const std::string &amount, const char *label)
    {
        if (is_zero_word(uint256_word(amount, label)))
            throw std::invalid_argument(std::string(label) + " must be positive");
    }

    bool uint256_is_zero(const std::string &value)
    {
        return is_zero_word(uint256_word(value, "amount"));
    }
} // namespace polymarket::detail
