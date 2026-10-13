#include "polymarket/clob_client.hpp"
#include "clob_client_internal.hpp"
#include "polymarket/pagination.hpp"

#include <nlohmann/json.hpp>

#include <exception>
#include <string>

using json = nlohmann::json;

namespace polymarket
{
    using namespace detail;

    namespace
    {
        template <typename T, typename Get, typename ParseItem>
        std::vector<T> collect_reward_pages(const std::string &endpoint, Get get,
                                            ParseItem parse_item)
        {
            auto paginator = Paginator<T>::cursor(
                endpoint,
                [&](const PageRequest &request) -> Result<Page<T>>
                {
                    const auto response = get(request.cursor);
                    if (!response.ok())
                        return Result<Page<T>>::failure(make_sdk_error(response, endpoint));
                    try
                    {
                        const json body = json::parse(response.body);
                        if (!body.is_object() || !body.at("data").is_array() ||
                            !body.at("next_cursor").is_string())
                            return Result<Page<T>>::failure(
                                make_parse_error("rewards page must hold data and next_cursor",
                                                 endpoint, response.body));
                        Page<T> page;
                        page.items.reserve(body.at("data").size());
                        for (const auto &item : body.at("data"))
                            page.items.push_back(parse_item(item));
                        page.next_cursor = body.at("next_cursor").get<std::string>();
                        return Result<Page<T>>::success(std::move(page));
                    }
                    catch (const std::exception &ex)
                    {
                        return Result<Page<T>>::failure(
                            make_parse_error(ex.what(), endpoint, response.body));
                    }
                },
                INITIAL_CURSOR, END_CURSOR);
            auto items = paginator.collect();
            return items ? std::move(items.value()) : std::vector<T>{};
        }
    } // namespace

    std::vector<ClobClient::RewardsInfo> ClobClient::get_rewards_markets_current()
    {
        return collect_reward_pages<RewardsInfo>(
            "/rewards/markets/current",
            [this](const std::string &cursor)
            {
                return read(
                    [&]
                    {
                        return http_.get("/rewards/markets/current?next_cursor=" +
                                         percent_encode_query_value(cursor));
                    });
            },
            parse_current_reward_info);
    }

    std::vector<ClobClient::RewardsInfo> ClobClient::get_rewards_markets(
        const std::string &condition_id)
    {
        if (condition_id.empty())
            return {};

        const std::string endpoint = "/rewards/markets/" + condition_id;
        return collect_reward_pages<RewardsInfo>(
            endpoint,
            [this, &endpoint](const std::string &cursor)
            {
                const std::string path =
                    endpoint + "?next_cursor=" + percent_encode_query_value(cursor);
                return read([&] { return http_.get(path); });
            },
            parse_market_reward_info);
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
        return collect_reward_pages<EarningsInfo>(
            endpoint,
            [this, &date](const std::string &cursor)
            {
                const std::string path =
                    std::string(endpoint) + "?date=" + percent_encode_query_value(date) +
                    "&signature_type=" + std::to_string(static_cast<int>(sig_type_)) +
                    "&next_cursor=" + percent_encode_query_value(cursor);
                return read([&] { return http_.get(path, get_l2_headers("GET", endpoint, "")); });
            },
            parse_user_earning_info);
    }
}
