#include "data_query.hpp"

#include <stdexcept>
#include <string>

// Market analytics queries, following py-sdk _internal/actions/data.py.
namespace polymarket::detail
{
    namespace
    {
        constexpr std::int64_t max_price_window_seconds = 15 * 86400;
    } // namespace

    std::string data_holders_query(const std::vector<std::string> &condition_ids,
                                   const DataHolderFilter &filter)
    {
        const auto conditions = data_market_condition_ids(condition_ids);
        if (conditions.empty()) throw std::invalid_argument("condition_ids is required");
        data_check_amount("min_balance", filter.min_balance);
        if (filter.include_pnl.value_or(false) && conditions.size() != 1)
            throw std::invalid_argument("include_pnl requires exactly one condition");
        QueryBuilder query;
        query.add_joined("condition_id", conditions);
        query.add("min_balance", filter.min_balance);
        query.add("include_pnl", filter.include_pnl);
        return query.str();
    }

    std::string data_open_interest_query(const std::vector<std::string> &condition_ids)
    {
        QueryBuilder query;
        query.add_joined("condition_id", data_market_condition_ids(condition_ids));
        return query.str();
    }

    std::string data_live_volume_query(const std::vector<std::int64_t> &event_ids)
    {
        const auto events = data_event_ids(event_ids);
        if (events.empty()) throw std::invalid_argument("event_ids is required");
        QueryBuilder query;
        query.add_joined("event_id", events);
        return query.str();
    }

    std::string data_price_history_query(const std::string &asset_id,
                                         const DataPriceHistoryFilter &filter,
                                         std::size_t page_size, std::int64_t now)
    {
        check_choice("interval", filter.interval, {"max", "all", "1m", "1w", "1d", "6h", "1h"});
        if (asset_id.empty()) throw std::invalid_argument("asset_id must be a non-empty string");
        const int selectors =
            !filter.interval.empty() + filter.start.has_value() + filter.as_of.has_value();
        if (selectors != 1)
            throw std::invalid_argument("Provide exactly one of interval, start, or as_of");
        if (filter.end && !filter.start) throw std::invalid_argument("end requires start");
        const auto start = data_timestamp(filter.start);
        const auto end = data_timestamp(filter.end);
        const auto as_of = data_timestamp(filter.as_of);
        if (start)
        {
            const auto until = end.value_or(now);
            if (until < *start || until - *start > max_price_window_seconds)
                throw std::invalid_argument(
                    "Price history windows must not end before start or exceed 15 days");
        }
        if (as_of && (filter.bucket_seconds || page_size != data_price_history_page_size))
            throw std::invalid_argument("as_of forbids bucket_seconds and page_size");
        if (filter.bucket_seconds)
        {
            const auto &interval = filter.interval;
            const std::int64_t minimum = interval == "max" || interval == "all" || interval == "1m"
                                             ? 600
                                         : interval == "1w" ? 300
                                                            : 60;
            if (*filter.bucket_seconds < minimum || *filter.bucket_seconds > 86400)
                throw std::invalid_argument("bucket_seconds must be between " +
                                            std::to_string(minimum) + " and 86400");
        }
        QueryBuilder query;
        query.add("token_id", asset_id);
        query.add("interval", filter.interval);
        query.add("start", start);
        query.add("end", end);
        query.add("as_of", as_of);
        query.add("bucket_seconds", filter.bucket_seconds);
        return query.str();
    }

    std::string data_resolution_query(const DataResolutionFilter &filter)
    {
        const int selectors =
            !filter.question_id.empty() + !filter.condition_ids.empty() + !filter.event_ids.empty();
        if (selectors != 1)
            throw std::invalid_argument(
                "Provide exactly one of question_id, condition_ids, or event_ids");
        const auto conditions = data_market_condition_ids(filter.condition_ids);
        const auto events = data_event_ids(filter.event_ids);
        if (events.size() > 20)
            throw std::invalid_argument("event_ids accepts at most 20 distinct values");
        QueryBuilder query;
        query.add("question_id", filter.question_id);
        query.add_joined("condition_id", conditions);
        query.add_joined("event_id", events);
        return query.str();
    }
} // namespace polymarket::detail
