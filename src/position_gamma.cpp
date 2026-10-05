#include "evm_uint.hpp"
#include "evm_utils.hpp"
#include "position_calls.hpp"
#include <stdexcept>

// Gamma /markets parsing for PositionClient.
namespace polymarket::detail
{
    namespace
    {
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

    MarketPositionContext market_context_from_gamma(const nlohmann::json &markets,
                                                    const std::string &condition_id,
                                                    const PolymarketContracts &contracts)
    {
        const auto wanted = normalize_requested_condition_id(condition_id);
        const bool bytes31_request = wanted.size() == 64;
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
        if (!market.contains("conditionId") || !market.at("conditionId").is_string())
            unexpected_market(wanted, "missing conditionId");
        const auto gamma_id = evm_normalize_hex(market.at("conditionId").get<std::string>());
        // Gamma may list a V2 condition as bytes31 or as bytes32 with an
        // outcome byte; either form matches a request in the other.
        bool matches = gamma_id == wanted;
        if (!matches && gamma_id.size() != wanted.size())
        {
            try
            {
                matches = v2_condition_id_bytes31(gamma_id) == v2_condition_id_bytes31(wanted);
            }
            catch (const std::invalid_argument &)
            {
            }
        }
        if (!matches)
            unexpected_market(wanted, "conditionId does not match the request");
        context.condition_id = gamma_id;
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
        if (bytes31_request)
            throw std::invalid_argument("condition " + wanted + " is a v1 market, which needs a 32-byte condition_id");
        if (gamma_id.size() != 66)
            unexpected_market(wanted, "v1 market has a conditionId that is not 32 bytes");

        context.protocol = MarketProtocol::Ctf;
        context.token_ids = binary_ids(string_sequence(market, "clobTokenIds", wanted), "clobTokenIds", wanted);
        if (!market.contains("negRisk") || !market.at("negRisk").is_boolean())
            unexpected_market(wanted, "missing negRisk flag");
        context.neg_risk = market.at("negRisk").get<bool>();
        context.position_token_contract = context.neg_risk ? contracts.neg_risk_adapter : contracts.conditional_tokens;
        context.operator_contract = contracts.ctf_collateral_adapter(context.neg_risk);
        return context;
    }
} // namespace polymarket::detail
