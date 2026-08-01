#include "http_client.hpp"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <thread>

namespace polymarket
{
    namespace
    {
        constexpr long heartbeat_timeout_ms = 75;

        class HeartbeatOptionReset
        {
        public:
            HeartbeatOptionReset(CURL *curl, long timeout_ms)
                : curl_(curl), timeout_ms_(timeout_ms) {}

            ~HeartbeatOptionReset()
            {
                curl_easy_setopt(curl_, CURLOPT_XFERINFOFUNCTION, nullptr);
                curl_easy_setopt(curl_, CURLOPT_XFERINFODATA, nullptr);
                curl_easy_setopt(curl_, CURLOPT_NOPROGRESS, 1L);
                curl_easy_setopt(curl_, CURLOPT_TIMEOUT_MS, timeout_ms_);
            }

        private:
            CURL *curl_;
            long timeout_ms_;
        };
    }

    bool HttpClient::warm_connection()
    {
        std::lock_guard<std::recursive_mutex> curl_lock(curl_mutex_);
        if (base_url_.empty())
        {
            return false;
        }

        const auto response = get("/");
        if (!response.ok() && response.status_code != 404)
        {
            return false;
        }
        std::lock_guard<std::mutex> lock(stats_mutex_);
        connection_warm_ = true;
        return true;
    }

    void HttpClient::start_heartbeat(long interval_seconds)
    {
        if (interval_seconds <= 0)
        {
            throw std::invalid_argument("heartbeat interval must be positive");
        }
        if (heartbeat_running_.exchange(true))
        {
            return;
        }

        heartbeat_thread_ = std::thread([this, interval_seconds]()
                                        {
            while (heartbeat_running_.load())
            {
                for (long i = 0; i < interval_seconds * 10 && heartbeat_running_.load(); ++i)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                if (heartbeat_running_.load())
                {
                    heartbeat_once();
                }
            } });
    }

    int HttpClient::heartbeat_progress(void *client, curl_off_t, curl_off_t,
                                       curl_off_t, curl_off_t)
    {
        return static_cast<HttpClient *>(client)->foreground_waiters_.load() > 0 ? 1 : 0;
    }

    void HttpClient::heartbeat_once()
    {
        if (foreground_waiters_.load() > 0)
            return;

        std::unique_lock<std::recursive_mutex> lock(curl_mutex_, std::try_to_lock);
        if (!lock.owns_lock() || foreground_waiters_.load() > 0 || base_url_.empty())
            return;

        const long configured_timeout = options_.timeout_ms;
        const long bounded_timeout = configured_timeout > 0
                                         ? std::min(configured_timeout, heartbeat_timeout_ms)
                                         : heartbeat_timeout_ms;
        HeartbeatOptionReset reset(curl_, configured_timeout);
        curl_easy_setopt(curl_, CURLOPT_TIMEOUT_MS, bounded_timeout);
        curl_easy_setopt(curl_, CURLOPT_XFERINFOFUNCTION, &HttpClient::heartbeat_progress);
        curl_easy_setopt(curl_, CURLOPT_XFERINFODATA, this);
        curl_easy_setopt(curl_, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl_, CURLOPT_HTTPGET, 1L);
        curl_easy_setopt(curl_, CURLOPT_POST, 0L);
        perform("HEARTBEAT", "/", base_url_ + "/");
    }

    void HttpClient::stop_heartbeat()
    {
        heartbeat_running_.store(false);
        if (heartbeat_thread_.joinable())
        {
            heartbeat_thread_.join();
        }
    }

    bool HttpClient::is_heartbeat_running() const
    {
        return heartbeat_running_.load();
    }

    HttpClient::ConnectionStats HttpClient::get_stats() const
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        return {total_requests_, reused_connections_, curl_errors_, bytes_received_, status_counts_,
                total_requests_ > 0 ? total_latency_ms_ / total_requests_ : 0.0,
                last_latency_ms_, min_latency_ms_, max_latency_ms_, connection_warm_};
    }

    RequestMetrics HttpClient::get_last_request_metrics() const
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        return last_metrics_;
    }
} // namespace polymarket
