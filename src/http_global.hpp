#pragma once

#include <cstddef>

namespace polymarket::detail
{
    struct HttpGlobalState
    {
        std::size_t client_references;
        bool manual_reference;
        bool initialized;
    };

    void acquire_http_global();
    void release_http_global() noexcept;
    void acquire_manual_http_global();
    void release_manual_http_global() noexcept;

    // Internal observer used by focused lifetime tests.
    HttpGlobalState http_global_state();
} // namespace polymarket::detail
