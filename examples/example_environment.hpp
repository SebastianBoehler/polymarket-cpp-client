#pragma once

#include "polymarket/environment.hpp"
#include <cstdlib>

namespace polymarket_example
{
    // POLYMARKET_ENV selects "production" (default) or "preproduction".
    // Throws std::invalid_argument for any other name.
    inline polymarket::Environment selected_environment()
    {
        const char *name = std::getenv("POLYMARKET_ENV");
        return polymarket::Environment::from_name(name && *name ? name : "production");
    }
} // namespace polymarket_example
