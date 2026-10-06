// Approval and transfer calldata, the required approval set, and state
// decoding. Calldata vectors are from py-sdk
// tests/unit/test_relayer_call_encoders_golden.py (b543c9d).
#include "approval_calls.hpp"
#include "check_support.hpp"
#include "polymarket/evm_abi.hpp"

using namespace polymarket;
using check_support::check;
using check_support::expect_equal;
using check_support::expect_throws;

namespace
{
    const std::string usdc_e = "0x2791Bca1f2de4661ED88A30C99A7a9449Aa84174";
    const std::string ctf = "0x4D97DCd97eC945f40cF65F87097ACe5EA0476045";
    const std::string neg_risk_adapter = "0xd91E80cF2E7be2e162c6513ceD06f1dD0dA35296";
    const std::string dead = "0x000000000000000000000000000000000000dEaD";
    const std::string owner = "0x9d8A62f656a8d1615C1294fd71e9CFb3E4855A4F";
    const auto mainnet = PolymarketContracts::polygon_mainnet();

    std::string word(const std::string &value)
    {
        return to_hex(evm_abi_encode({EvmAbiValue::uint256(value)}));
    }

    void golden_calldata()
    {
        const auto approve = detail::erc20_approve_call(usdc_e, ctf, max_uint256);
        check(approve.to == usdc_e && approve.value == "0", "approve targets the token");
        expect_equal("approve(CTF, max)", approve.data,
                     "0x095ea7b3"
                     "0000000000000000000000004d97dcd97ec945f40cf65f87097ace5ea0476045"
                     "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        expect_equal("transfer(dead, 1)", detail::erc20_transfer_call(usdc_e, dead, "1").data,
                     "0xa9059cbb"
                     "000000000000000000000000000000000000000000000000000000000000dead"
                     "0000000000000000000000000000000000000000000000000000000000000001");
        expect_equal("setApprovalForAll(adapter, true)",
                     detail::erc1155_set_approval_for_all_call(ctf, neg_risk_adapter, true).data,
                     "0xa22cb465"
                     "000000000000000000000000d91e80cf2e7be2e162c6513ced06f1dd0da35296"
                     "0000000000000000000000000000000000000000000000000000000000000001");
        expect_equal("setApprovalForAll(adapter, false)",
                     detail::erc1155_set_approval_for_all_call(ctf, neg_risk_adapter, false).data,
                     "0xa22cb465"
                     "000000000000000000000000d91e80cf2e7be2e162c6513ced06f1dd0da35296"
                     "0000000000000000000000000000000000000000000000000000000000000000");
        expect_equal("allowance(owner, CTF)", detail::erc20_allowance_call(usdc_e, owner, ctf).data,
                     "0xdd62ed3e"
                     "0000000000000000000000009d8a62f656a8d1615c1294fd71e9cfb3e4855a4f"
                     "0000000000000000000000004d97dcd97ec945f40cf65f87097ace5ea0476045");
        expect_equal("isApprovedForAll(owner, adapter)",
                     detail::erc1155_is_approved_for_all_call(ctf, owner, neg_risk_adapter).data,
                     "0xe985e9c5"
                     "0000000000000000000000009d8a62f656a8d1615c1294fd71e9cfb3e4855a4f"
                     "000000000000000000000000d91e80cf2e7be2e162c6513ced06f1dd0da35296");
    }

    void required_set()
    {
        const auto required = required_trading_approvals(mainnet);
        check(required.erc20.size() == 7 && required.erc1155.size() == 10,
              "7 ERC-20 + 10 ERC-1155 approvals");
        const std::vector<std::string> spenders = {mainnet.standard_exchange,
                                                   mainnet.neg_risk_exchange,
                                                   mainnet.collateral_adapter,
                                                   mainnet.neg_risk_collateral_adapter,
                                                   mainnet.protocol_v2_router,
                                                   mainnet.exchange_v3,
                                                   "0xDCa4af75705dbB50f62437045afF9921947917d2"};
        for (size_t i = 0; i < required.erc20.size() && i < spenders.size(); ++i)
        {
            const auto &a = required.erc20[i];
            check(a.token_address == mainnet.collateral_token && a.spender == spenders[i] &&
                      a.amount == max_uint256,
                  "ERC-20 approval " + std::to_string(i));
        }
        const std::vector<std::pair<std::string, std::string>> operators = {
            {ctf, mainnet.standard_exchange},
            {ctf, mainnet.neg_risk_exchange},
            {ctf, mainnet.collateral_adapter},
            {ctf, mainnet.neg_risk_collateral_adapter},
            {ctf, mainnet.auto_redeem_operator},
            {ctf, mainnet.binary_module},
            {ctf, mainnet.neg_risk_module},
            {mainnet.position_manager, mainnet.protocol_v2_router},
            {mainnet.position_manager, mainnet.exchange_v3},
            {mainnet.position_manager, mainnet.auto_redeem_operator}};
        for (size_t i = 0; i < required.erc1155.size() && i < operators.size(); ++i)
        {
            const auto &a = required.erc1155[i];
            check(a.token_address == operators[i].first &&
                      a.operator_address == operators[i].second,
                  "ERC-1155 approval " + std::to_string(i));
        }
        check(word(max_uint256) == "0x" + std::string(64, 'f'), "max_uint256 is 2^256 - 1");
    }

    void state_from_results()
    {
        const auto required = required_trading_approvals(mainnet);
        const auto checks = detail::trading_approval_check_calls(required, owner);
        check(checks.size() == 17 && checks[0].to == mainnet.collateral_token &&
                  checks[0].data == detail::erc20_allowance_call(mainnet.collateral_token, owner,
                                                                 mainnet.standard_exchange)
                                        .data &&
                  checks[16].to == mainnet.position_manager,
              "check calls follow the required order");

        std::vector<std::string> results(7, word(max_uint256));
        results.resize(17, word("1"));
        auto state = detail::trading_approvals_state_from_results(required, results);
        check(state.is_fully_approved && state.missing.empty(), "everything approved");

        // A large but finite allowance is still missing, as in the official SDKs.
        results[2] = word("1000000000000");
        results[0] = word("0");
        results[9] = word("0");
        state = detail::trading_approvals_state_from_results(required, results);
        check(!state.is_fully_approved && state.missing.erc20.size() == 2 &&
                  state.missing.erc1155.size() == 1,
              "missing counts");
        check(state.missing.erc20[0].spender == mainnet.standard_exchange &&
                  state.missing.erc20[1].spender == mainnet.collateral_adapter &&
                  state.missing.erc1155[0].operator_address == mainnet.collateral_adapter,
              "missing entries keep the required order");

        const auto calls = detail::trading_approval_calls(state.missing);
        check(calls.size() == 3 && calls[0].data.starts_with("0x095ea7b3") &&
                  calls[1].data.starts_with("0x095ea7b3") && calls[2].to == ctf &&
                  calls[2].data.ends_with(std::string(63, '0') + "1"),
              "approve calls first, then setApprovalForAll(true)");

        auto bad = results;
        bad[0] = "0x";
        expect_throws<std::runtime_error>(
            "empty eth_call result (no contract)",
            [&] { (void)detail::trading_approvals_state_from_results(required, bad); });
        bad = results;
        bad[10] = word("2");
        expect_throws<std::runtime_error>(
            "non-bool isApprovedForAll",
            [&] { (void)detail::trading_approvals_state_from_results(required, bad); });
    }

    void input_validation()
    {
        expect_equal("max amount", detail::resolve_approval_amount("max"), max_uint256);
        expect_equal("zero amount revokes", detail::resolve_approval_amount("0"), "0");
        expect_throws<std::invalid_argument>("negative amount",
                                             [] { (void)detail::resolve_approval_amount("-1"); });
        expect_throws<std::invalid_argument>(
            "amount above uint256",
            [] { (void)detail::resolve_approval_amount(std::string(max_uint256) + "0"); });
        detail::require_address(owner, "wallet");
        expect_throws<std::invalid_argument>(
            "unprefixed address", [] { detail::require_address(owner.substr(2), "wallet"); });
        expect_throws<std::invalid_argument>(
            "short address", [] { detail::require_address(owner.substr(0, 40), "wallet"); });
        expect_throws<std::invalid_argument>(
            "non-hex address",
            [] { detail::require_address("0x" + std::string(40, 'g'), "wallet"); });
    }
} // namespace

int main()
{
    golden_calldata();
    required_set();
    state_from_results();
    input_validation();
    return check_support::finish("test_approval_calls");
}
