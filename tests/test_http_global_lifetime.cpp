#include "http_client.hpp"
#include "http_global.hpp"

#include <condition_variable>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace polymarket;

namespace
{
    bool check(bool condition, const std::string &message)
    {
        if (!condition)
            std::cerr << message << '\n';
        return condition;
    }

    bool check_state(std::size_t clients, bool manual, bool initialized,
                     const std::string &context)
    {
        const auto state = detail::http_global_state();
        return check(state.client_references == clients,
                     context + ": unexpected client reference count") &&
               check(state.manual_reference == manual,
                     context + ": unexpected manual reference state") &&
               check(state.initialized == initialized,
                     context + ": unexpected initialization state");
    }
} // namespace

int main()
{
    bool ok = true;
    http_global_cleanup();
    ok &= check_state(0, false, false, "initial state");

    {
        HttpClient client;
        ok &= check_state(1, false, true, "automatic client acquisition");
        client.set_timeout_ms(25);
    }
    ok &= check_state(0, false, false, "automatic client release");

    http_global_init();
    http_global_init();
    ok &= check_state(0, true, true, "idempotent manual initialization");
    {
        HttpClient client;
        ok &= check_state(1, true, true, "manual and client ownership");
        http_global_cleanup();
        ok &= check_state(1, false, true, "manual cleanup with live client");
        client.set_dns_cache_timeout(2);
    }
    ok &= check_state(0, false, false, "last client releases global state");
    http_global_cleanup();

    {
        HttpClient destination;
        HttpClient source;
        ok &= check_state(2, false, true, "two move sources");
        destination = std::move(source);
        ok &= check_state(1, false, true, "move assignment");
        HttpClient final_client(std::move(destination));
        ok &= check_state(1, false, true, "move construction");
    }
    ok &= check_state(0, false, false, "moved client release");

    constexpr std::size_t thread_count = 8;
    std::mutex barrier_mutex;
    std::condition_variable barrier_cv;
    std::size_t ready = 0;
    bool release = false;
    std::vector<std::thread> workers;
    workers.reserve(thread_count);
    for (std::size_t i = 0; i < thread_count; ++i)
    {
        workers.emplace_back([&]
                             {
            HttpClient client;
            std::unique_lock<std::mutex> lock(barrier_mutex);
            ++ready;
            barrier_cv.notify_all();
            barrier_cv.wait(lock, [&] { return release; }); });
    }

    {
        std::unique_lock<std::mutex> lock(barrier_mutex);
        barrier_cv.wait(lock, [&] { return ready == thread_count; });
    }
    ok &= check_state(thread_count, false, true, "concurrent acquisition");
    http_global_init();
    ok &= check_state(thread_count, true, true, "concurrent clients plus manual owner");
    http_global_cleanup();
    ok &= check_state(thread_count, false, true, "concurrent clients after manual cleanup");
    {
        std::lock_guard<std::mutex> lock(barrier_mutex);
        release = true;
    }
    barrier_cv.notify_all();
    for (auto &worker : workers)
        worker.join();
    ok &= check_state(0, false, false, "concurrent release");

    return ok ? 0 : 1;
}
