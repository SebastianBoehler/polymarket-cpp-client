#include "data_parsing.hpp"

#include <stdexcept>
#include <string>

// Field names and rules follow py-sdk models/data/leaderboard.py.
namespace polymarket::detail
{
    namespace
    {
        std::optional<std::int64_t> standing_rank(const json &item, const char *key)
        {
            const auto rank = field_integer(item, key);
            if (rank && *rank == 0) return std::nullopt;
            return rank;
        }

        std::string positive_event_id(const json &item, const char *key)
        {
            const auto id = field_required_id(item, key);
            if (id.find_first_not_of("0123456789") != std::string::npos ||
                id.find_first_not_of('0') == std::string::npos)
                throw_bad_field(key, "a positive event ID");
            return id;
        }
    } // namespace

    DataTraderEntry parse_data_trader_entry(const json &item)
    {
        DataTraderEntry entry;
        entry.rank = data_required_integer(item, "rank");
        entry.wallet = field_required_string(item, "user_id");
        entry.pnl = data_required_number(item, "pnl");
        entry.volume = data_required_number(item, "volume");
        entry.user_name = field_string(item, "user_name");
        entry.profile_image = field_string(item, "profile_image");
        entry.x_username = field_string(item, "x_username");
        entry.verified = data_required_bool(item, "verified");
        entry.raw = item;
        return entry;
    }

    DataBiggestWinner parse_data_biggest_winner(const json &item)
    {
        DataBiggestWinner winner;
        winner.kind = data_choice(item, "kind", {"market", "combo"}, "market or combo");
        winner.rank = data_required_integer(item, "win_rank");
        winner.wallet = field_required_string(item, "user_id");
        winner.pnl = data_required_number(item, "pnl");
        winner.initial_value = data_required_number(item, "initial_value");
        winner.final_value = data_required_number(item, "final_value");
        winner.resolved_at = data_epoch_or_iso(item, "resolved_at");
        winner.user_name = field_string(item, "user_name");
        winner.profile_image = field_string(item, "profile_image");
        winner.event_title = field_string(item, "event_title");
        winner.asset_id = field_required_string(item, "position_id");
        if (winner.kind == "combo")
        {
            winner.condition_id = data_combo_condition_id(item, "condition_id");
        }
        else
        {
            winner.condition_id = data_required_hex(item, "condition_id");
            winner.event_id = positive_event_id(item, "event_id");
            winner.event_slug = field_string(item, "event_slug");
        }
        winner.raw = item;
        return winner;
    }

    Page<DataTraderEntry> parse_data_trader_leaderboard_page(const std::string &body)
    {
        return data_page<DataTraderEntry>(body, parse_data_trader_entry);
    }

    std::optional<DataTraderStanding> parse_data_trader_standing(const std::string &body)
    {
        const auto parsed = json::parse(body);
        const auto &data = data_envelope(parsed);
        if (data.is_null()) return std::nullopt;
        if (!data.is_object()) throw std::invalid_argument("Data API data must be an object");
        DataTraderStanding standing;
        standing.wallet = field_required_string(data, "user_id");
        standing.pnl = data_required_number(data, "pnl");
        standing.volume = data_required_number(data, "volume");
        standing.pnl_rank = standing_rank(data, "rank_pnl");
        standing.volume_rank = standing_rank(data, "rank_volume");
        standing.user_name = field_string(data, "user_name");
        standing.profile_image = field_string(data, "profile_image");
        standing.x_username = field_string(data, "x_username");
        standing.verified = data_required_bool(data, "verified");
        standing.raw = data;
        return standing;
    }

    Page<DataBiggestWinner> parse_data_biggest_winners_page(const std::string &body)
    {
        return data_page<DataBiggestWinner>(body, parse_data_biggest_winner);
    }
} // namespace polymarket::detail
