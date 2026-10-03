// Calldata vectors were generated with the official py-sdk call builders
// (polymarket._internal.actions.relayer.calls).
#include "position_test_support.hpp"

using namespace position_test;

namespace
{
    void calldata_vectors()
    {
        expect_equal("neg-risk adapter split",
                     detail::ctf_split_position_call(kContracts.neg_risk_collateral_adapter,
                                                     kContracts.collateral_token, kCondition, "2500000")
                         .data,
                     "0x72ce4275000000000000000000000000c011a7e12a19f7b1f670d46f03b03f3342e82dfb0000000000000000"
                     "0000000000000000000000000000000000000000000000006b049bc3befa02c22d0cfaee77625fc94e9a6d571f00"
                     "c015ba7a3b442e306fbc00000000000000000000000000000000000000000000000000000000000000a000000000"
                     "000000000000000000000000000000000000000000000000002625a0000000000000000000000000000000000000"
                     "0000000000000000000000000002000000000000000000000000000000000000000000000000000000000000000100"
                     "00000000000000000000000000000000000000000000000000000000000002");
        expect_equal("adapter redeem",
                     detail::ctf_redeem_positions_call(kContracts.collateral_adapter, kContracts.collateral_token,
                                                       kCondition)
                         .data,
                     "0x01b7037c000000000000000000000000c011a7e12a19f7b1f670d46f03b03f3342e82dfb0000000000000000"
                     "0000000000000000000000000000000000000000000000006b049bc3befa02c22d0cfaee77625fc94e9a6d571f00"
                     "c015ba7a3b442e306fbc000000000000000000000000000000000000000000000000000000000000008000000000"
                     "0000000000000000000000000000000000000000000000000000000200000000000000000000000000000000000000"
                     "000000000000000000000000010000000000000000000000000000000000000000000000000000000000000002");
        std::string v2_condition = "0x";
        for (int i = 0; i < 31; ++i)
            v2_condition += "ab";
        expect_equal("router split",
                     detail::router_split_call(kContracts.protocol_v2_router, v2_condition + "01", "1000000").data,
                     "0x82d3b9f1ababababababababababababababababababababababababababababababab00000000000000000000"
                     "00000000000000000000000000000000000000000f4240");
        expect_equal("router merge",
                     detail::router_merge_call(kContracts.protocol_v2_router, v2_condition, "7").data,
                     "0x5b63685eababababababababababababababababababababababababababababababab00000000000000000000"
                     "0000000000000000000000000000000000000000000007");
        expect_equal("router redeem",
                     detail::router_redeem_call(kContracts.protocol_v2_router, v2_condition + "01", 1, "61333300").data,
                     "0xd217a3ccababababababababababababababababababababababababababababababab00000000000000000000"
                     "0000000000000000000000000000000000000000000001000000000000000000000000000000000000000000000000"
                     "0000000003a7df34");

        MarketPositionContext market;
        market.token_ids = {kYes, kNo};
        market.position_token_contract = kContracts.conditional_tokens;
        const auto balance_call = detail::balance_of_batch_call(market, "0xa8Beb4744dc06f51355b92baa9a57dd7F127D006");
        check(balance_call.to == kContracts.conditional_tokens, "balanceOfBatch target");
        expect_equal("balanceOfBatch", balance_call.data,
                     "0x4e1273f4000000000000000000000000000000000000000000000000000000000000004000000000000000000000"
                     "000000000000000000000000000000000000000000a0000000000000000000000000000000000000000000000000"
                     "0000000000000002000000000000000000000000a8beb4744dc06f51355b92baa9a57dd7f127d006000000000000"
                     "000000000000a8beb4744dc06f51355b92baa9a57dd7f127d00600000000000000000000000000000000000000000000"
                     "0000000000000000000244ae1c02ca63bf316480f7afdc21d071e80fb84e921c257d5eeea383ca1e2c3e66b9e1fa39"
                     "b50d28bacceebb70a50f13bd5a77c61f78457a189487a0796e291c");

        expect_throws<std::invalid_argument>("v2 bytes32 with non-outcome last byte", [&]
                                             { (void)detail::v2_condition_id_bytes31(v2_condition + "02"); });
        expect_throws<std::invalid_argument>("short condition id", []
                                             { (void)detail::normalize_condition_id("0x1234"); });
        expect_throws<std::invalid_argument>("unprefixed condition id", []
                                             { (void)detail::normalize_condition_id(std::string(64, '1')); });
    }

    void gamma_parsing()
    {
        const auto ctf = detail::market_context_from_gamma(gamma_market(kCondition, "v1", false), kCondition, kContracts);
        check(ctf.protocol == MarketProtocol::Ctf && !ctf.neg_risk && ctf.market_id == "2959706" &&
                  ctf.token_ids[0] == kYes && ctf.token_ids[1] == kNo &&
                  ctf.position_token_contract == kContracts.conditional_tokens &&
                  ctf.operator_contract == kContracts.collateral_adapter,
              "v1 market context");

        const auto upper = detail::market_context_from_gamma(
            gamma_market("0x6B049BC3BEFA02C22D0CFAEE77625FC94E9A6D571F00C015BA7A3B442E306FBC", "v1", true),
            kCondition, kContracts);
        check(upper.neg_risk && upper.position_token_contract == kContracts.neg_risk_adapter &&
                  upper.operator_contract == kContracts.neg_risk_collateral_adapter,
              "neg-risk market context with uppercase condition id");

        const auto v2 = detail::market_context_from_gamma(gamma_market(kCondition2, "v2", false), kCondition2, kContracts);
        check(v2.protocol == MarketProtocol::V2 && v2.token_ids[0] == "100" && v2.token_ids[1] == "101" &&
                  v2.position_token_contract == kContracts.position_manager &&
                  v2.operator_contract == kContracts.protocol_v2_router,
              "v2 market context");

        expect_throws<std::invalid_argument>("no market", [&]
                                             { (void)detail::market_context_from_gamma(nlohmann::json::array(), kCondition, kContracts); });
        const auto rejects = [&](const std::string &name, const std::function<void(nlohmann::json &)> &mutate)
        {
            auto markets = gamma_market(kCondition, "v1", false);
            mutate(markets[0]);
            expect_throws<std::runtime_error>(name, [&]
                                              { (void)detail::market_context_from_gamma(markets, kCondition, kContracts); });
        };
        rejects("different condition", [](auto &m)
                { m["conditionId"] = kCondition2; });
        rejects("missing version", [](auto &m)
                { m.erase("version"); });
        rejects("unknown version", [](auto &m)
                { m["version"] = "v3"; });
        rejects("missing negRisk", [](auto &m)
                { m.erase("negRisk"); });
        rejects("three token ids", [](auto &m)
                { m["clobTokenIds"] = nlohmann::json::array({"1", "2", "3"}); });
        rejects("non-numeric token id", [](auto &m)
                { m["clobTokenIds"] = nlohmann::json::array({"1", "x"}); });
        rejects("unparseable token list", [](auto &m)
                { m["clobTokenIds"] = "[1,"; });
        expect_throws<std::runtime_error>("two markets", [&]
                                          {
            auto markets = gamma_market(kCondition, "v1", false);
            markets.push_back(markets[0]);
            (void)detail::market_context_from_gamma(markets, kCondition, kContracts); });
    }

    void merge_amounts()
    {
        expect_equal("max takes the smaller side", detail::resolve_merge_amount(kCondition, {"5000000", "3000000"}, "max"),
                     "3000000");
        expect_equal("explicit amount", detail::resolve_merge_amount(kCondition, {"5000000", "3000000"}, "2000000"),
                     "2000000");
        expect_equal("uint256-scale balances",
                     detail::resolve_merge_amount(kCondition, {"100000000000000000000000000000", "99999999999999999999999999999"}, "max"),
                     "99999999999999999999999999999");
        expect_throws<std::invalid_argument>("amount above max", []
                                             { (void)detail::resolve_merge_amount(kCondition, {"5", "3"}, "4"); });
        expect_throws<std::invalid_argument>("one side empty", []
                                             { (void)detail::resolve_merge_amount(kCondition, {"5", "0"}, "max"); });
        expect_throws<std::invalid_argument>("zero amount", []
                                             { (void)detail::resolve_merge_amount(kCondition, {"5", "3"}, "0"); });
        expect_throws<std::invalid_argument>("decimal amount", []
                                             { (void)detail::resolve_merge_amount(kCondition, {"5", "3"}, "1.5"); });
    }
} // namespace

int main()
{
    calldata_vectors();
    gamma_parsing();
    merge_amounts();
    return check_support::finish("test_position_calls");
}
