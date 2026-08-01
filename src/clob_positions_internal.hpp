#pragma once

#include "clob_types.hpp"
#include "http_client.hpp"

#include <functional>
#include <string>
#include <vector>

namespace polymarket::detail
{
    enum class PositionFilter
    {
        all,
        redeemable,
        mergeable
    };

    using PositionFetch = std::function<HttpResponse(const std::string &)>;

    std::vector<Position> fetch_position_pages(const std::string &address,
                                               PositionFilter filter,
                                               const PositionFetch &fetch);
}
