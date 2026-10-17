#include "data_parsing.hpp"

#include <cctype>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <string_view>

// Field names and rules follow py-sdk models/data/portfolio.py and common.py.
namespace polymarket::detail
{
    namespace
    {
        std::string choice(const json &item, const char *key,
                           std::initializer_list<std::string_view> choices, const char *expected)
        {
            auto value = field_required_string(item, key);
            for (const auto allowed : choices)
            {
                if (value == allowed) return value;
            }
            throw_bad_field(key, expected);
        }

        std::string event_id(const json &item, const char *key)
        {
            auto id = field_id(item, key);
            return id == "0" ? std::string() : id;
        }

        std::string calendar_date(const json &item, const char *key)
        {
            auto date = field_string(item, key);
            if (date.empty() || date == "1970-01-01") return {};
            bool valid = date.size() == 10 && date[4] == '-' && date[7] == '-';
            for (std::size_t i = 0; valid && i < date.size(); ++i)
                valid = i == 4 || i == 7 || std::isdigit(static_cast<unsigned char>(date[i]));
            if (!valid) throw_bad_field(key, "a YYYY-MM-DD date");
            return date;
        }

        const json &envelope_object(const json &body)
        {
            const auto &data = data_envelope(body);
            if (!data.is_object()) throw std::invalid_argument("Data API data must be an object");
            return data;
        }
    } // namespace

    DataPosition parse_data_position(const json &item)
    {
        DataPosition position;
        position.wallet = field_required_string(item, "proxy_wallet");
        position.asset_id = field_required_string(item, "token_id");
        position.condition_id = data_required_hex(item, "condition_id");
        position.current_size = data_required_number(item, "current_size");
        position.avg_price = data_required_number(item, "avg_price");
        position.entry_cost_usdc = data_required_number(item, "entry_cost_usdc");
        position.entry_fees_usdc = data_required_number(item, "entry_fees_usdc");
        position.total_cost_usdc = data_required_number(item, "total_cost_usdc");
        position.current_price = data_required_number(item, "current_price");
        position.current_value = data_required_number(item, "current_value");
        position.total_size = data_required_number(item, "total_size");
        position.realized_pnl = data_required_number(item, "realized_pnl");
        position.unrealized_pnl = data_required_number(item, "unrealized_pnl");
        position.total_pnl = data_required_number(item, "total_pnl");
        position.percent_pnl = data_required_number(item, "percent_pnl");
        position.percent_realized_pnl = data_required_number(item, "percent_realized_pnl");
        position.status =
            choice(item, "status", {"OPEN", "REDEEMABLE", "CLOSED"}, "OPEN, REDEEMABLE or CLOSED");
        position.redeemable = data_required_bool(item, "redeemable");
        position.mergeable = data_required_bool(item, "mergeable");
        position.negative_risk = data_required_bool(item, "negative_risk");
        position.archived = data_required_bool(item, "archived");
        position.verified = data_required_bool(item, "verified");
        position.title = field_string(item, "title");
        position.slug = field_string(item, "slug");
        position.icon = field_string(item, "icon");
        position.event_slug = field_string(item, "event_slug");
        position.event_id = event_id(item, "event_id");
        position.outcome = field_string(item, "outcome");
        position.outcome_index = data_outcome_index(item, "outcome_index");
        position.opposite_outcome = field_string(item, "opposite_outcome");
        position.opposite_asset_id = field_string(item, "opposite_token_id");
        position.end_date = calendar_date(item, "end_date");
        position.last_event_at = data_optional_epoch_seconds(item, "last_event_at");
        position.first_entry_at = data_optional_epoch_seconds(item, "first_entry_at");
        position.name = field_string(item, "name");
        position.profile_image = field_string(item, "profile_image");
        position.raw = item;
        return position;
    }

    DataPnlPoint parse_data_pnl_point(const json &item)
    {
        DataPnlPoint point;
        point.timestamp = data_epoch_seconds(item, "timestamp");
        point.source_block = data_required_integer(item, "source_block");
        point.trade_count = data_required_integer(item, "trade_count");
        point.realized_market_pnl = data_required_number(item, "realized_market_pnl");
        point.realized_lp_pnl = data_required_number(item, "realized_lp_pnl");
        point.realized_combo_pnl = data_required_number(item, "realized_combo_pnl");
        point.realized_pnl = data_required_number(item, "realized_pnl");
        point.volume = data_required_number(item, "volume");
        point.volume_usdc = data_required_number(item, "volume_usdc");
        point.unrealized_pnl = field_number(item, "unrealized_pnl");
        point.fees_refunded = field_number(item, "fees_refunded");
        point.maker_rebate = field_number(item, "maker_rebate");
        point.taker_rebate = field_number(item, "taker_rebate");
        point.reward_income = field_number(item, "reward_income");
        point.yield_income = field_number(item, "yield_income");
        point.referral_income = field_number(item, "referral_income");
        point.sponsored_income = field_number(item, "sponsored_income");
        point.deposits = field_number(item, "deposits");
        point.withdrawals = field_number(item, "withdrawals");
        point.cashflow_net = field_number(item, "cashflow_net");
        point.wallet_income = field_number(item, "wallet_income");
        point.position_pnl = field_number(item, "position_pnl");
        point.settled_pnl = field_number(item, "settled_pnl");
        point.economic_pnl = field_number(item, "economic_pnl");
        point.trade_pnl = field_number(item, "trade_pnl");
        point.fees = field_number(item, "fees");
        point.fees_paid = field_number(item, "fees_paid");
        point.raw = item;
        return point;
    }

    Page<DataPosition> parse_data_position_page(const std::string &body)
    {
        return data_page<DataPosition>(body, parse_data_position);
    }

    DataPortfolioValue parse_data_portfolio_value(const std::string &body)
    {
        const auto parsed = json::parse(body);
        const auto &data = envelope_object(parsed);
        DataPortfolioValue value;
        value.wallet = field_required_string(data, "proxy_wallet");
        value.value = data_required_number(data, "value");
        value.raw = data;
        return value;
    }

    std::optional<DataUserStats> parse_data_user_stats(const std::string &body)
    {
        const auto parsed = json::parse(body);
        if (data_envelope(parsed).is_null()) return std::nullopt;
        const auto &data = envelope_object(parsed);
        DataUserStats stats;
        stats.wallet = field_required_string(data, "proxy_wallet");
        stats.traded_market_count = data_required_integer(data, "trades");
        stats.biggest_win = data_required_number(data, "biggest_win");
        stats.views = data_required_integer(data, "views");
        stats.join_date = data_optional_epoch_seconds(data, "join_date");
        if (const auto *pnl = find_field(data, "all_time_pnl"))
            stats.all_time_pnl = parse_data_pnl_point(*pnl);
        stats.raw = data;
        return stats;
    }

    DataPnlSeries parse_data_pnl_series(const std::string &body)
    {
        const auto parsed = json::parse(body);
        const auto &data = envelope_object(parsed);
        constexpr const char *fidelities = "1d, 18h, 12h, 3h or 1h";
        DataPnlSeries series;
        series.wallet = field_required_string(data, "proxy_wallet");
        series.interval = choice(data, "interval", {"max", "all", "1m", "1w", "1d", "12h", "6h"},
                                 "max, all, 1m, 1w, 1d, 12h or 6h");
        series.fidelity = choice(data, "fidelity", {"1d", "18h", "12h", "3h", "1h"}, fidelities);
        series.source_fidelity =
            choice(data, "source_fidelity", {"1d", "18h", "12h", "3h", "1h"}, fidelities);
        const auto *points = find_field(data, "points");
        if (points == nullptr || !points->is_array()) throw_bad_field("points", "an array");
        series.points.reserve(points->size());
        for (const auto &point : *points)
            series.points.push_back(parse_data_pnl_point(point));
        return series;
    }

    DataUserVolume parse_data_user_volume(const std::string &body)
    {
        const auto parsed = json::parse(body);
        const auto &data = envelope_object(parsed);
        DataUserVolume volume;
        volume.volume = data_required_number(data, "volume");
        volume.volume_usdc = data_required_number(data, "volume_usdc");
        volume.trade_count = data_required_integer(data, "trade_count");
        volume.raw = data;
        return volume;
    }
} // namespace polymarket::detail
