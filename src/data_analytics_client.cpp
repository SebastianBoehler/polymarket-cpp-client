#include "data_client_internal.hpp"
#include "data_parsing.hpp"
#include "data_query.hpp"

#include <chrono>
#include <string>

namespace polymarket
{
    namespace
    {
        constexpr std::size_t max_pnl_holders_page_size = 100;

        std::int64_t now_seconds()
        {
            return std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                .count();
        }
    } // namespace

    Paginator<DataAssetHolders>
    DataClient::holders_pager(const std::vector<std::string> &condition_ids,
                              const DataHolderFilter &filter, std::size_t page_size,
                              const PageRequest &resume)
    {
        const auto cap = filter.include_pnl.value_or(false) ? max_pnl_holders_page_size
                                                            : detail::data_max_page_size;
        return cursor_pager<DataAssetHolders>(
            "/v2/holders", page_size, cap, resume,
            [&] { return detail::data_holders_query(condition_ids, filter); },
            detail::parse_data_holders_page);
    }

    Result<std::vector<DataOpenInterest>>
    DataClient::get_open_interests(const std::vector<std::string> &condition_ids)
    {
        return fetch<std::vector<DataOpenInterest>>(
            [&] { return "/v2/oi" + detail::data_open_interest_query(condition_ids); },
            detail::parse_data_open_interests);
    }

    Result<DataLiveVolume>
    DataClient::get_event_live_volume(const std::vector<std::int64_t> &event_ids)
    {
        return fetch<DataLiveVolume>(
            [&] { return "/v2/live-volume" + detail::data_live_volume_query(event_ids); },
            detail::parse_data_live_volume);
    }

    Paginator<DataPricePoint> DataClient::price_history_pager(const std::string &asset_id,
                                                              const DataPriceHistoryFilter &filter,
                                                              std::size_t page_size,
                                                              const PageRequest &resume)
    {
        return cursor_pager<DataPricePoint>(
            "/v2/prices-history", page_size, detail::data_price_history_page_size, resume,
            [&]
            {
                return detail::data_price_history_query(asset_id, filter, page_size, now_seconds());
            },
            detail::parse_data_price_history_page);
    }

    Result<std::vector<DataResolution>>
    DataClient::get_resolutions(const DataResolutionFilter &filter)
    {
        return fetch<std::vector<DataResolution>>(
            [&] { return "/v2/resolutions" + detail::data_resolution_query(filter); },
            detail::parse_data_resolutions);
    }
} // namespace polymarket
