#include "approval_calls.hpp"
#include "evm_uint.hpp"
#include "polymarket/evm_abi.hpp"
#include "polymarket/evm_utils.hpp"
#include <cctype>
#include <stdexcept>

namespace polymarket::detail
{
    namespace
    {
        std::string single_word(const std::string &return_data, const std::string &context)
        {
            std::vector<std::string> words;
            try
            {
                words = evm_data_words(return_data);
            }
            catch (const std::invalid_argument &error)
            {
                throw std::runtime_error("invalid " + context + " result: " + error.what());
            }
            if (words.size() != 1)
                throw std::runtime_error("invalid " + context + " result: expected one word, got " +
                                         std::to_string(words.size()));
            return words[0];
        }

        std::vector<uint8_t> amount_word(const std::string &amount, const char *label)
        {
            try
            {
                return parse_uint256_word(amount);
            }
            catch (const std::invalid_argument &error)
            {
                throw std::invalid_argument(std::string(label) + ": " + error.what());
            }
        }
    } // namespace

    void require_address(const std::string &value, const char *label)
    {
        bool ok = value.size() == 42 && value.rfind("0x", 0) == 0;
        for (size_t i = 2; ok && i < value.size(); ++i)
            ok = std::isxdigit(static_cast<unsigned char>(value[i])) != 0;
        if (!ok)
            throw std::invalid_argument(std::string(label) +
                                        " must be 0x followed by 20 bytes of hex, got \"" + value +
                                        "\"");
    }

    std::string resolve_approval_amount(const std::string &amount)
    {
        if (amount == "max") return max_uint256;
        (void)amount_word(amount, "approval amount");
        return amount;
    }

    ContractCall erc20_approve_call(const std::string &token, const std::string &spender,
                                    const std::string &amount)
    {
        (void)amount_word(amount, "approval amount");
        return {token,
                evm_abi_encode_call("approve(address,uint256)",
                                    {EvmAbiValue::address(spender), EvmAbiValue::uint256(amount)})};
    }

    ContractCall erc20_transfer_call(const std::string &token, const std::string &recipient,
                                     const std::string &amount)
    {
        (void)amount_word(amount, "transfer amount");
        return {token,
                evm_abi_encode_call("transfer(address,uint256)", {EvmAbiValue::address(recipient),
                                                                  EvmAbiValue::uint256(amount)})};
    }

    ContractCall erc1155_set_approval_for_all_call(const std::string &token,
                                                   const std::string &operator_address,
                                                   bool approved)
    {
        return {token, evm_abi_encode_call("setApprovalForAll(address,bool)",
                                           {EvmAbiValue::address(operator_address),
                                            EvmAbiValue::boolean(approved)})};
    }

    ContractCall erc20_allowance_call(const std::string &token, const std::string &owner,
                                      const std::string &spender)
    {
        return {token,
                evm_abi_encode_call("allowance(address,address)",
                                    {EvmAbiValue::address(owner), EvmAbiValue::address(spender)})};
    }

    ContractCall erc1155_is_approved_for_all_call(const std::string &token,
                                                  const std::string &owner,
                                                  const std::string &operator_address)
    {
        return {token, evm_abi_encode_call(
                           "isApprovedForAll(address,address)",
                           {EvmAbiValue::address(owner), EvmAbiValue::address(operator_address)})};
    }

    std::string decode_uint256_result(const std::string &return_data, const std::string &context)
    {
        return evm_uint_word_to_decimal(single_word(return_data, context));
    }

    bool decode_bool_result(const std::string &return_data, const std::string &context)
    {
        const auto value = decode_uint256_result(return_data, context);
        if (value != "0" && value != "1")
            throw std::runtime_error("invalid " + context + " result: not a bool");
        return value == "1";
    }

    std::vector<ContractCall> trading_approval_check_calls(const TradingApprovals &required,
                                                           const std::string &owner)
    {
        std::vector<ContractCall> calls;
        calls.reserve(required.erc20.size() + required.erc1155.size());
        for (const auto &approval : required.erc20)
            calls.push_back(erc20_allowance_call(approval.token_address, owner, approval.spender));
        for (const auto &approval : required.erc1155)
            calls.push_back(erc1155_is_approved_for_all_call(approval.token_address, owner,
                                                             approval.operator_address));
        return calls;
    }

    TradingApprovalsState
    trading_approvals_state_from_results(const TradingApprovals &required,
                                         const std::vector<std::string> &results)
    {
        if (results.size() != required.erc20.size() + required.erc1155.size())
            throw std::logic_error("trading approval results do not match the checks");
        TradingApprovalsState state;
        size_t index = 0;
        for (const auto &approval : required.erc20)
        {
            const auto context =
                "allowance(" + approval.token_address + ", " + approval.spender + ")";
            const auto allowance = decode_uint256_result(results[index++], context);
            if (parse_uint256_word(allowance) < amount_word(approval.amount, "required amount"))
                state.missing.erc20.push_back(approval);
        }
        for (const auto &approval : required.erc1155)
        {
            const auto context = "isApprovedForAll(" + approval.token_address + ", " +
                                 approval.operator_address + ")";
            if (!decode_bool_result(results[index++], context))
                state.missing.erc1155.push_back(approval);
        }
        state.is_fully_approved = state.missing.empty();
        return state;
    }

    std::vector<ContractCall> trading_approval_calls(const TradingApprovals &missing)
    {
        std::vector<ContractCall> calls;
        calls.reserve(missing.erc20.size() + missing.erc1155.size());
        for (const auto &approval : missing.erc20)
            calls.push_back(
                erc20_approve_call(approval.token_address, approval.spender, approval.amount));
        for (const auto &approval : missing.erc1155)
            calls.push_back(erc1155_set_approval_for_all_call(approval.token_address,
                                                              approval.operator_address, true));
        return calls;
    }
} // namespace polymarket::detail

namespace polymarket
{
    TradingApprovals required_trading_approvals(const PolymarketContracts &contracts)
    {
        const auto &pusd = contracts.collateral_token;
        const auto &ctf = contracts.conditional_tokens;
        const auto &positions = contracts.position_manager;
        TradingApprovals required;
        for (const auto *spender :
             {&contracts.standard_exchange, &contracts.neg_risk_exchange,
              &contracts.collateral_adapter, &contracts.neg_risk_collateral_adapter,
              &contracts.protocol_v2_router, &contracts.exchange_v3,
              &contracts.perps_deposit_contract})
            required.erc20.push_back({pusd, *spender, max_uint256});
        for (const auto *operator_address :
             {&contracts.standard_exchange, &contracts.neg_risk_exchange,
              &contracts.collateral_adapter, &contracts.neg_risk_collateral_adapter,
              &contracts.auto_redeem_operator, &contracts.binary_module,
              &contracts.neg_risk_module})
            required.erc1155.push_back({ctf, *operator_address});
        for (const auto *operator_address : {&contracts.protocol_v2_router, &contracts.exchange_v3,
                                             &contracts.auto_redeem_operator})
            required.erc1155.push_back({positions, *operator_address});
        return required;
    }
} // namespace polymarket
