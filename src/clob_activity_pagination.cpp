#include "clob_client.hpp"
#include "clob_client_internal.hpp"
#include "opaque_cursor_pagination.hpp"

#include <nlohmann/json.hpp>

#include <iterator>

using json = nlohmann::json;

namespace polymarket
{
    using namespace detail;

    std::vector<ClobClient::RewardsInfo> ClobClient::get_rewards_markets_current()
    {
        std::vector<RewardsInfo> result;
        OpaqueCursorPagination pagination(INITIAL_CURSOR, END_CURSOR);
        while (true)
        {
            try
            {
                if (!pagination.begin_page())
                    break;
            }
            catch (const OpaqueCursorPaginationError &)
            {
                return {};
            }

            auto response = http_.get("/rewards/markets/current?next_cursor=" +
                                      percent_encode_query_value(pagination.cursor()));
            if (!response.ok())
                return {};
            try
            {
                const auto page = json::parse(response.body);
                if (!page.is_object() || !page.at("data").is_array() ||
                    !page.at("next_cursor").is_string())
                    return {};
                std::vector<RewardsInfo> parsed_page;
                parsed_page.reserve(page.at("data").size());
                for (const auto &item : page.at("data"))
                    parsed_page.push_back(parse_current_reward_info(item));
                result.insert(result.end(), std::make_move_iterator(parsed_page.begin()),
                              std::make_move_iterator(parsed_page.end()));
                pagination.advance(page.at("next_cursor").get<std::string>());
            }
            catch (...)
            {
                return {};
            }
        }
        return result;
    }

    std::vector<ClobClient::RewardsInfo> ClobClient::get_rewards_markets(
        const std::string &condition_id)
    {
        if (condition_id.empty())
            return {};

        std::vector<RewardsInfo> result;
        OpaqueCursorPagination pagination(INITIAL_CURSOR, END_CURSOR);
        while (true)
        {
            try
            {
                if (!pagination.begin_page())
                    break;
            }
            catch (const OpaqueCursorPaginationError &)
            {
                return {};
            }

            const std::string path = "/rewards/markets/" + condition_id +
                                     "?next_cursor=" +
                                     percent_encode_query_value(pagination.cursor());
            auto response = http_.get(path);
            if (!response.ok())
                return {};
            try
            {
                const auto page = json::parse(response.body);
                if (!page.is_object() || !page.at("data").is_array() ||
                    !page.at("next_cursor").is_string())
                    return {};
                std::vector<RewardsInfo> parsed_page;
                parsed_page.reserve(page.at("data").size());
                for (const auto &item : page.at("data"))
                    parsed_page.push_back(parse_market_reward_info(item));
                result.insert(result.end(), std::make_move_iterator(parsed_page.begin()),
                              std::make_move_iterator(parsed_page.end()));
                pagination.advance(page.at("next_cursor").get<std::string>());
            }
            catch (...)
            {
                return {};
            }
        }
        return result;
    }

    std::optional<ClobClient::EarningsInfo> ClobClient::get_earnings_for_user_for_day(
        const std::string &date)
    {
        const auto earnings = get_earnings_for_user_for_day_all(date);
        if (earnings.empty())
            return std::nullopt;
        return earnings.front();
    }

    std::vector<ClobClient::EarningsInfo> ClobClient::get_earnings_for_user_for_day_all(
        const std::string &date)
    {
        constexpr const char *endpoint = "/rewards/user";
        std::vector<EarningsInfo> result;
        OpaqueCursorPagination pagination(INITIAL_CURSOR, END_CURSOR);
        while (true)
        {
            try
            {
                if (!pagination.begin_page())
                    break;
            }
            catch (const OpaqueCursorPaginationError &)
            {
                return {};
            }

            const std::string path = std::string(endpoint) +
                                     "?date=" + percent_encode_query_value(date) +
                                     "&signature_type=" + std::to_string(static_cast<int>(sig_type_)) +
                                     "&next_cursor=" + percent_encode_query_value(pagination.cursor());
            auto headers = get_l2_headers("GET", endpoint, "");
            auto response = http_.get(path, headers);
            if (!response.ok())
                return {};
            try
            {
                const auto page = json::parse(response.body);
                if (!page.is_object() || !page.at("data").is_array() ||
                    !page.at("next_cursor").is_string())
                    return {};
                std::vector<EarningsInfo> parsed_page;
                parsed_page.reserve(page.at("data").size());
                for (const auto &item : page.at("data"))
                    parsed_page.push_back(parse_user_earning_info(item));
                result.insert(result.end(), std::make_move_iterator(parsed_page.begin()),
                              std::make_move_iterator(parsed_page.end()));
                pagination.advance(page.at("next_cursor").get<std::string>());
            }
            catch (...)
            {
                return {};
            }
        }
        return result;
    }
}
