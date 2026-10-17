#include "data_parsing.hpp"

#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

// Field names and rules follow py-sdk models/data/analytics.py and resolutions.py.
namespace polymarket::detail
{
    namespace
    {
        const json &envelope_list(const json &body)
        {
            const auto &data = data_envelope(body);
            if (!data.is_array()) throw std::invalid_argument("Data API data must be an array");
            return data;
        }

        std::optional<double> oracle_price(const json &item, const char *key)
        {
            const auto *value = find_field(item, key);
            if (value != nullptr && (*value == 69 || *value == "69")) return std::nullopt;
            return field_number(item, key);
        }

        std::optional<std::int64_t> log_index(const json &item, const char *key)
        {
            const auto *value = find_field(item, key);
            if (value == nullptr || *value == "") return std::nullopt;
            if (value->is_string())
            {
                const auto &text = value->get_ref<const std::string &>();
                if (text.size() > 18 || text.find_first_not_of("0123456789") != std::string::npos)
                    throw_bad_field(key, "an integer");
                return std::stoll(text);
            }
            return field_integer(item, key);
        }

        std::optional<std::pair<double, double>> payouts(const json &item)
        {
            const auto *value = find_field(item, "payouts");
            if (value == nullptr) return std::nullopt;
            if (!value->is_array() || value->size() != 2)
                throw_bad_field("payouts", "a payout pair");
            const json pair = {{"0", value->at(0)}, {"1", value->at(1)}};
            const auto first = field_number(pair, "0");
            const auto second = field_number(pair, "1");
            if (!first || !second || !std::isfinite(*first) || !std::isfinite(*second))
                throw_bad_field("payouts", "a pair of finite amounts");
            return std::pair{*first / 1e6, *second / 1e6};
        }

        DataAssetHolders parse_asset_holders(const json &item)
        {
            DataAssetHolders group;
            group.asset_id = field_required_string(item, "token_id");
            const auto *holders = find_field(item, "holders");
            if (holders == nullptr || !holders->is_array()) throw_bad_field("holders", "an array");
            group.holders.reserve(holders->size());
            for (const auto &holder : *holders)
                group.holders.push_back(parse_data_holder(holder));
            return group;
        }

        DataPricePoint parse_price_point(const json &item)
        {
            DataPricePoint point;
            point.timestamp = data_epoch_seconds(item, "timestamp");
            point.price = data_required_number(item, "price");
            point.resolution_seconds = data_required_integer(item, "resolution_seconds");
            return point;
        }
    } // namespace

    DataHolder parse_data_holder(const json &item)
    {
        DataHolder holder;
        holder.wallet = field_required_string(item, "proxy_wallet");
        holder.asset_id = field_required_string(item, "token_id");
        holder.amount = data_required_number(item, "amount");
        holder.outcome_index = data_outcome_index(item, "outcome_index");
        holder.display_username_public = data_required_bool(item, "display_username_public");
        holder.verified = data_required_bool(item, "verified");
        holder.name = field_string(item, "name");
        holder.pseudonym = field_string(item, "pseudonym");
        holder.bio = field_string(item, "bio");
        holder.profile_image = field_string(item, "profile_image");
        holder.profile_image_optimized = field_string(item, "profile_image_optimized");
        holder.avg_price = field_number(item, "avg_price");
        holder.entry_cost_usdc = field_number(item, "entry_cost_usdc");
        holder.current_price = field_number(item, "current_price");
        holder.current_value = field_number(item, "current_value");
        holder.realized_pnl = field_number(item, "realized_pnl");
        holder.unrealized_pnl = field_number(item, "unrealized_pnl");
        holder.total_pnl = field_number(item, "total_pnl");
        holder.raw = item;
        return holder;
    }

    DataResolution parse_data_resolution(const json &item)
    {
        DataResolution resolution;
        resolution.question_id = field_string(item, "question_id");
        resolution.condition_id = field_hex(item, "condition_id");
        resolution.status =
            data_choice(item, "status",
                        {"initialized", "posed", "proposed", "challenged", "reproposed", "disputed",
                         "active", "arbitration", "resolved"},
                        "a resolution status");
        resolution.extended_review = data_required_bool(item, "extended_review");
        resolution.expected_settlement_time =
            data_optional_epoch_or_iso(item, "expected_settlement_time");
        resolution.settlement_time_basis =
            data_optional_choice(item, "settlement_time_basis",
                                 {"managed_proposal_expiration", "proposal_expiration", "liveness",
                                  "dvm_round_estimate"},
                                 "a settlement time basis");
        resolution.was_disputed = data_required_bool(item, "was_disputed");
        resolution.question_rules_updated = data_required_bool(item, "new_version_q");
        resolution.proposed_price = oracle_price(item, "proposed_price");
        resolution.reproposed_price = oracle_price(item, "reproposed_price");
        resolution.price = oracle_price(item, "price");
        resolution.transaction_hash = field_string(item, "transaction_hash");
        resolution.log_index = log_index(item, "log_index");
        resolution.last_updated_at = data_epoch_or_iso(item, "last_update_timestamp");
        resolution.market_type = data_optional_choice(
            item, "market_type", {"BINARY", "INCREMENTAL_NEGRISK", "ATOMIC_NEGRISK"},
            "a market type");
        resolution.payouts = payouts(item);
        resolution.resolution_source = data_optional_choice(
            item, "resolution_source", {"reported", "derived"}, "reported or derived");
        resolution.reporter = data_optional_choice(item, "reporter", {"UMA_OO", "CHAINLINK", "EOA"},
                                                   "UMA_OO, CHAINLINK or EOA");
        resolution.was_arbitrated = field_bool(item, "was_arbitrated");
        resolution.resolved_block = field_integer(item, "resolved_block");
        resolution.resolved_at = data_optional_epoch_or_iso(item, "resolved_at");
        resolution.raw = item;
        return resolution;
    }

    Page<DataAssetHolders> parse_data_holders_page(const std::string &body)
    {
        return data_page<DataAssetHolders>(body, parse_asset_holders);
    }

    std::vector<DataOpenInterest> parse_data_open_interests(const std::string &body)
    {
        const auto parsed = json::parse(body);
        std::vector<DataOpenInterest> interests;
        for (const auto &item : envelope_list(parsed))
        {
            DataOpenInterest interest;
            const auto *condition = find_field(item, "condition_id");
            if (condition == nullptr || *condition != "GLOBAL")
                interest.condition_id = field_hex(item, "condition_id");
            interest.value = data_required_number(item, "value");
            interest.raw = item;
            interests.push_back(std::move(interest));
        }
        return interests;
    }

    DataLiveVolume parse_data_live_volume(const std::string &body)
    {
        const auto parsed = json::parse(body);
        const auto &data = data_envelope(parsed);
        if (!data.is_object()) throw std::invalid_argument("Data API data must be an object");
        DataLiveVolume volume;
        volume.taker_volume_total = data_required_number(data, "taker_volume_total");
        const auto *markets = find_field(data, "conditions");
        if (markets == nullptr || !markets->is_array()) throw_bad_field("conditions", "an array");
        for (const auto &item : *markets)
        {
            DataMarketLiveVolume market;
            market.condition_id = field_hex(item, "condition_id");
            market.taker_volume = data_required_number(item, "taker_volume");
            volume.markets.push_back(std::move(market));
        }
        volume.raw = data;
        return volume;
    }

    Page<DataPricePoint> parse_data_price_history_page(const std::string &body)
    {
        return data_page<DataPricePoint>(body, parse_price_point);
    }

    std::vector<DataResolution> parse_data_resolutions(const std::string &body)
    {
        const auto parsed = json::parse(body);
        std::vector<DataResolution> resolutions;
        for (const auto &item : envelope_list(parsed))
            resolutions.push_back(parse_data_resolution(item));
        return resolutions;
    }
} // namespace polymarket::detail
