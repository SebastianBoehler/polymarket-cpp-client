#include "position_calls.hpp"
#include "evm_abi.hpp"
#include "evm_uint.hpp"
#include "evm_utils.hpp"
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

        [[noreturn]] void unexpected_market(const std::string &condition_id, const std::string &reason)
        {
            throw std::runtime_error("unexpected Gamma market for condition " + condition_id + ": " + reason);
        }

        // Gamma returns some id lists as JSON arrays and others as JSON-encoded strings.
        std::vector<std::string> string_sequence(const nlohmann::json &market, const char *key,
                                                 const std::string &condition_id)
        {
            if (!market.contains(key) || market.at(key).is_null())
                return {};
            nlohmann::json value = market.at(key);
            if (value.is_string())
            {
                try
                {
                    value = nlohmann::json::parse(value.get<std::string>());
                }
                catch (const nlohmann::json::parse_error &)
                {
                    unexpected_market(condition_id, std::string(key) + " is not a JSON list");
                }
            }
            if (!value.is_array())
                unexpected_market(condition_id, std::string(key) + " is not a list");
            std::vector<std::string> out;
            for (const auto &item : value)
            {
                if (!item.is_string())
                    unexpected_market(condition_id, std::string(key) + " must contain strings");
                out.push_back(item.get<std::string>());
            }
            return out;
        }

        std::array<std::string, 2> binary_ids(const std::vector<std::string> &ids, const char *key,
                                              const std::string &condition_id)
        {
            if (ids.size() != 2)
                unexpected_market(condition_id, std::string(key) + " must hold exactly two ids");
            for (const auto &id : ids)
            {
                try
                {
                    (void)parse_uint256_word(id);
                }
                catch (const std::invalid_argument &)
                {
                    unexpected_market(condition_id, std::string(key) + " holds a non-integer id");
                }
            }
            return {ids[0], ids[1]};
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

    MarketPositionContext market_context_from_gamma(const nlohmann::json &markets,
                                                    const std::string &condition_id,
                                                    const PolymarketContracts &contracts)
    {
        const auto wanted = normalize_condition_id(condition_id);
        if (!markets.is_array())
            unexpected_market(wanted, "response is not a list");
        if (markets.empty())
            throw std::invalid_argument("no market found for condition " + wanted);
        if (markets.size() != 1)
            unexpected_market(wanted, "expected one market, got " + std::to_string(markets.size()));
        const auto &market = markets.front();
        if (!market.is_object())
            unexpected_market(wanted, "entry is not an object");

        MarketPositionContext context;
        if (!market.contains("conditionId") || !market.at("conditionId").is_string() ||
            evm_normalize_hex(market.at("conditionId").get<std::string>()) != wanted)
            unexpected_market(wanted, "conditionId does not match the request");
        context.condition_id = wanted;
        if (market.contains("id") && market.at("id").is_string())
            context.market_id = market.at("id").get<std::string>();
        else if (market.contains("id") && market.at("id").is_number_integer())
            context.market_id = std::to_string(market.at("id").get<long long>());

        if (!market.contains("version") || !market.at("version").is_string())
            unexpected_market(wanted, "missing market version");
        const auto version = market.at("version").get<std::string>();

        if (version == "v2")
        {
            context.protocol = MarketProtocol::V2;
            context.token_ids = binary_ids(string_sequence(market, "positionIds", wanted), "positionIds", wanted);
            context.position_token_contract = contracts.position_manager;
            context.operator_contract = contracts.protocol_v2_router;
            (void)v2_condition_id_bytes31(wanted);
            return context;
        }
        if (version != "v1")
            unexpected_market(wanted, "unknown market version " + version);

        context.protocol = MarketProtocol::Ctf;
        context.token_ids = binary_ids(string_sequence(market, "clobTokenIds", wanted), "clobTokenIds", wanted);
        if (!market.contains("negRisk") || !market.at("negRisk").is_boolean())
            unexpected_market(wanted, "missing negRisk flag");
        context.neg_risk = market.at("negRisk").get<bool>();
        context.position_token_contract = context.neg_risk ? contracts.neg_risk_adapter : contracts.conditional_tokens;
        context.operator_contract = contracts.ctf_collateral_adapter(context.neg_risk);
        return context;
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
