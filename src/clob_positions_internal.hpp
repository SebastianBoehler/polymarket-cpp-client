#pragma once

#include "polymarket/clob_types.hpp"
#include "polymarket/http_client.hpp"
#include "polymarket/pagination.hpp"

#include <cstddef>
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

    Paginator<Position> position_paginator(const std::string &address, PositionFilter filter,
                                           PositionFetch fetch, size_t start_offset = 0);

    std::vector<Position> fetch_position_pages(const std::string &address,
                                               PositionFilter filter,
                                               const PositionFetch &fetch);
}
