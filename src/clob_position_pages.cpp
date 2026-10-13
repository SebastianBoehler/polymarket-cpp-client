#include "clob_positions_internal.hpp"

#include "query_encoding.hpp"
#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace polymarket::detail
{
    namespace
    {
        using json = nlohmann::json;
        constexpr size_t PAGE_LIMIT = 500;
        constexpr size_t MAX_OFFSET = 10'000;

        std::string required_string(const json &item, const char *field)
        {
            const auto &value = item.at(field);
            if (!value.is_string())
                throw std::invalid_argument(std::string(field) + " must be a string");
            return value.get<std::string>();
        }

        double required_number(const json &item, const char *field)
        {
            const auto &value = item.at(field);
            if (!value.is_number())
                throw std::invalid_argument(std::string(field) + " must be a number");
            const double number = value.get<double>();
            if (!std::isfinite(number))
                throw std::invalid_argument(std::string(field) + " must be finite");
            return number;
        }

        bool required_bool(const json &item, const char *field)
        {
            const auto &value = item.at(field);
            if (!value.is_boolean())
                throw std::invalid_argument(std::string(field) + " must be a boolean");
            return value.get<bool>();
        }

        int required_int(const json &item, const char *field)
        {
            const auto &value = item.at(field);
            if (value.is_number_unsigned())
            {
                const auto number = value.get<uint64_t>();
                if (number > static_cast<uint64_t>(std::numeric_limits<int>::max()))
                    throw std::out_of_range(std::string(field) + " is out of range");
                return static_cast<int>(number);
            }
            if (!value.is_number_integer())
                throw std::invalid_argument(std::string(field) + " must be an integer");
            const auto number = value.get<int64_t>();
            if (number < std::numeric_limits<int>::min() ||
                number > std::numeric_limits<int>::max())
                throw std::out_of_range(std::string(field) + " is out of range");
            return static_cast<int>(number);
        }

        Position parse_position(const json &item)
        {
            if (!item.is_object())
                throw std::invalid_argument("position must be an object");
            Position position;
            position.proxy_wallet = required_string(item, "proxyWallet");
            position.asset = required_string(item, "asset");
            position.condition_id = required_string(item, "conditionId");
            position.size = required_number(item, "size");
            position.avg_price = required_number(item, "avgPrice");
            position.initial_value = required_number(item, "initialValue");
            position.current_value = required_number(item, "currentValue");
            position.cash_pnl = required_number(item, "cashPnl");
            position.percent_pnl = required_number(item, "percentPnl");
            position.total_bought = required_number(item, "totalBought");
            position.realized_pnl = required_number(item, "realizedPnl");
            position.percent_realized_pnl = required_number(item, "percentRealizedPnl");
            position.cur_price = required_number(item, "curPrice");
            position.redeemable = required_bool(item, "redeemable");
            position.mergeable = required_bool(item, "mergeable");
            position.title = required_string(item, "title");
            position.slug = required_string(item, "slug");
            position.icon = required_string(item, "icon");
            position.event_slug = required_string(item, "eventSlug");
            position.outcome = required_string(item, "outcome");
            position.outcome_index = required_int(item, "outcomeIndex");
            position.opposite_outcome = required_string(item, "oppositeOutcome");
            position.opposite_asset = required_string(item, "oppositeAsset");
            position.end_date = required_string(item, "endDate");
            position.negative_risk = required_bool(item, "negativeRisk");
            return position;
        }

        std::vector<Position> parse_page(const std::string &body)
        {
            const auto items = json::parse(body);
            if (!items.is_array() || items.size() > PAGE_LIMIT)
                throw std::invalid_argument("positions response must be an array of at most 500 items");
            std::vector<Position> positions;
            positions.reserve(items.size());
            for (const auto &item : items)
                positions.push_back(parse_position(item));
            return positions;
        }

        std::string filter_query(PositionFilter filter)
        {
            switch (filter)
            {
            case PositionFilter::redeemable:
                return "&redeemable=true";
            case PositionFilter::mergeable:
                return "&mergeable=true";
            case PositionFilter::all:
                return {};
            }
            throw std::invalid_argument("invalid position filter");
        }
    }

    Paginator<Position> position_paginator(const std::string &address, PositionFilter filter,
                                           PositionFetch fetch, size_t start_offset)
    {
        constexpr const char *endpoint = "/positions";
        if (address.empty())
        {
            SdkError error;
            error.code = SdkErrorCode::InvalidArgument;
            error.message = "positions require a user address";
            error.endpoint = endpoint;
            return Paginator<Position>::offset(
                endpoint,
                [error](const PageRequest &) { return Result<Page<Position>>::failure(error); },
                PAGE_LIMIT, MAX_OFFSET, start_offset);
        }
        if (!fetch) throw std::invalid_argument("position fetch must be set");
        std::string prefix = std::string(endpoint) +
                             "?user=" + percent_encode_query_value(address) + filter_query(filter) +
                             "&limit=500&offset=";
        return Paginator<Position>::offset(
            endpoint,
            [prefix = std::move(prefix),
             fetch = std::move(fetch)](const PageRequest &request) -> Result<Page<Position>>
            {
                const auto response = fetch(prefix + std::to_string(request.offset));
                if (!response.ok())
                    return Result<Page<Position>>::failure(make_sdk_error(response, endpoint));
                try
                {
                    return Result<Page<Position>>::success(
                        Page<Position>{parse_page(response.body), ""});
                }
                catch (const std::exception &ex)
                {
                    return Result<Page<Position>>::failure(
                        make_parse_error(ex.what(), endpoint, response.body));
                }
            },
            PAGE_LIMIT, MAX_OFFSET, start_offset);
    }

    std::vector<Position> fetch_position_pages(const std::string &address,
                                               PositionFilter filter,
                                               const PositionFetch &fetch)
    {
        if (address.empty() || !fetch)
            return {};
        try
        {
            auto positions = position_paginator(address, filter, fetch).collect();
            return positions ? std::move(positions.value()) : std::vector<Position>{};
        }
        catch (...)
        {
            return {};
        }
    }
}
