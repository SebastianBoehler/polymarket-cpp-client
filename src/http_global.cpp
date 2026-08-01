#include "http_global.hpp"

#include <curl/curl.h>

#include <mutex>
#include <stdexcept>
#include <string>

namespace polymarket::detail
{
    namespace
    {
        struct GlobalRegistry
        {
            std::mutex mutex;
            std::size_t client_references{0};
            bool manual_reference{false};
            bool initialized{false};
        };

        GlobalRegistry &registry()
        {
            static GlobalRegistry value;
            return value;
        }

        void initialize_locked(GlobalRegistry &state)
        {
            if (state.initialized)
                return;

            const auto result = curl_global_init(CURL_GLOBAL_ALL);
            if (result != CURLE_OK)
            {
                throw std::runtime_error("Failed to initialize CURL globally: " +
                                         std::to_string(static_cast<int>(result)));
            }
            state.initialized = true;
        }

        void cleanup_if_unused_locked(GlobalRegistry &state) noexcept
        {
            if (!state.initialized || state.manual_reference || state.client_references != 0)
                return;
            curl_global_cleanup();
            state.initialized = false;
        }
    } // namespace

    void acquire_http_global()
    {
        auto &state = registry();
        std::lock_guard<std::mutex> lock(state.mutex);
        initialize_locked(state);
        ++state.client_references;
    }

    void release_http_global() noexcept
    {
        auto &state = registry();
        std::lock_guard<std::mutex> lock(state.mutex);
        if (state.client_references == 0)
            return;
        --state.client_references;
        cleanup_if_unused_locked(state);
    }

    void acquire_manual_http_global()
    {
        auto &state = registry();
        std::lock_guard<std::mutex> lock(state.mutex);
        if (state.manual_reference)
            return;
        initialize_locked(state);
        state.manual_reference = true;
    }

    void release_manual_http_global() noexcept
    {
        auto &state = registry();
        std::lock_guard<std::mutex> lock(state.mutex);
        if (!state.manual_reference)
            return;
        state.manual_reference = false;
        cleanup_if_unused_locked(state);
    }

    HttpGlobalState http_global_state()
    {
        auto &state = registry();
        std::lock_guard<std::mutex> lock(state.mutex);
        return {state.client_references, state.manual_reference, state.initialized};
    }
} // namespace polymarket::detail
