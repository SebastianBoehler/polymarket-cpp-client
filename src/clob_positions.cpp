#include "clob_client.hpp"
#include "clob_positions_internal.hpp"

namespace polymarket
{
    namespace
    {
        std::string position_address(const ClobClient &client,
                                     const std::string &requested)
        {
            if (!requested.empty())
                return requested;
            if (!client.get_funder_address().empty())
                return client.get_funder_address();
            return client.get_address();
        }
    }

    std::vector<ClobClient::Position> ClobClient::get_positions(
        const std::string &user_address)
    {
        return detail::fetch_position_pages(
            position_address(*this, user_address), detail::PositionFilter::all,
            [this](const std::string &path) { return data_http_.get(path); });
    }

    std::vector<ClobClient::Position> ClobClient::get_redeemable_positions(
        const std::string &user_address)
    {
        return detail::fetch_position_pages(
            position_address(*this, user_address),
            detail::PositionFilter::redeemable,
            [this](const std::string &path) { return data_http_.get(path); });
    }

    std::vector<ClobClient::Position> ClobClient::get_mergeable_positions(
        const std::string &user_address)
    {
        return detail::fetch_position_pages(
            position_address(*this, user_address), detail::PositionFilter::mergeable,
            [this](const std::string &path) { return data_http_.get(path); });
    }
}
