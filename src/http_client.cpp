#include "http_client.hpp"
#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <thread>

namespace polymarket
{

    // Global initialization
    static bool g_curl_initialized = false;

    void http_global_init()
    {
        if (!g_curl_initialized)
        {
            curl_global_init(CURL_GLOBAL_ALL);
            g_curl_initialized = true;
        }
    }

    void http_global_cleanup()
    {
        if (g_curl_initialized)
        {
            curl_global_cleanup();
            g_curl_initialized = false;
        }
    }

    HttpClient::HttpClient()
        : curl_(nullptr), headers_(nullptr),
          heartbeat_running_(false),
          total_requests_(0), reused_connections_(0),
          curl_errors_(0), bytes_received_(0),
          total_latency_ms_(0.0), last_latency_ms_(0.0),
          min_latency_ms_(0.0), max_latency_ms_(0.0),
          connection_warm_(false)
    {
        init();
    }

    HttpClient::HttpClient(const HttpClientOptions &options)
        : HttpClient()
    {
        configure(options);
    }

    HttpClient::~HttpClient()
    {
        stop_heartbeat();
        cleanup();
    }

    HttpClient::HttpClient(HttpClient &&other) noexcept
        : curl_(other.curl_), headers_(other.headers_), base_url_(std::move(other.base_url_)),
          proxy_url_(std::move(other.proxy_url_)), options_(other.options_),
          heartbeat_running_(false),
          total_requests_(other.total_requests_), reused_connections_(other.reused_connections_),
          curl_errors_(other.curl_errors_), bytes_received_(other.bytes_received_),
          status_counts_(std::move(other.status_counts_)),
          total_latency_ms_(other.total_latency_ms_), last_latency_ms_(other.last_latency_ms_),
          min_latency_ms_(other.min_latency_ms_), max_latency_ms_(other.max_latency_ms_),
          connection_warm_(other.connection_warm_), last_metrics_(std::move(other.last_metrics_))
    {
        other.stop_heartbeat();
        other.curl_ = nullptr;
        other.headers_ = nullptr;
    }

    HttpClient &HttpClient::operator=(HttpClient &&other) noexcept
    {
        if (this != &other)
        {
            stop_heartbeat();
            other.stop_heartbeat();
            cleanup();
            curl_ = other.curl_;
            headers_ = other.headers_;
            base_url_ = std::move(other.base_url_);
            proxy_url_ = std::move(other.proxy_url_);
            options_ = other.options_;
            total_requests_ = other.total_requests_;
            reused_connections_ = other.reused_connections_;
            curl_errors_ = other.curl_errors_;
            bytes_received_ = other.bytes_received_;
            status_counts_ = std::move(other.status_counts_);
            total_latency_ms_ = other.total_latency_ms_;
            last_latency_ms_ = other.last_latency_ms_;
            min_latency_ms_ = other.min_latency_ms_;
            max_latency_ms_ = other.max_latency_ms_;
            connection_warm_ = other.connection_warm_;
            last_metrics_ = std::move(other.last_metrics_);
            other.curl_ = nullptr;
            other.headers_ = nullptr;
        }
        return *this;
    }

    void HttpClient::init()
    {
        curl_ = curl_easy_init();
        if (!curl_)
        {
            throw std::runtime_error("Failed to initialize CURL");
        }

        curl_easy_setopt(curl_, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl_, CURLOPT_MAXREDIRS, 3L);
        curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, write_callback);
        apply_options();

        // HTTP/1.1 keep-alive
        add_header("Connection: keep-alive");

        // SSL options
        curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYHOST, 2L);

        // Default headers
        add_header("Accept: application/json");
        add_header("Content-Type: application/json");
    }

    void HttpClient::cleanup()
    {
        if (headers_)
        {
            curl_slist_free_all(headers_);
            headers_ = nullptr;
        }
        if (curl_)
        {
            curl_easy_cleanup(curl_);
            curl_ = nullptr;
        }
    }

    void HttpClient::set_timeout_ms(long timeout_ms)
    {
        options_.timeout_ms = timeout_ms;
        apply_options();
    }

    void HttpClient::set_base_url(const std::string &base_url)
    {
        base_url_ = base_url;
        // Remove trailing slash
        if (!base_url_.empty() && base_url_.back() == '/')
        {
            base_url_.pop_back();
        }
    }

    void HttpClient::add_header(const std::string &header)
    {
        headers_ = curl_slist_append(headers_, header.c_str());
    }

    void HttpClient::set_proxy(const std::string &proxy_url)
    {
        options_.proxy_url = proxy_url;
        proxy_url_ = proxy_url;
        if (curl_)
        {
            if (proxy_url_.empty())
            {
                curl_easy_setopt(curl_, CURLOPT_PROXY, nullptr);
                curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYPEER, 1L);
                curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYHOST, 2L);
                return;
            }

            curl_easy_setopt(curl_, CURLOPT_PROXY, proxy_url_.c_str());

            // Detect proxy type from URL scheme
            if (proxy_url_.find("socks5://") == 0 || proxy_url_.find("socks5h://") == 0)
            {
                // SOCKS5 proxy - use socks5h for DNS resolution through proxy
                curl_easy_setopt(curl_, CURLOPT_PROXYTYPE, CURLPROXY_SOCKS5_HOSTNAME);
            }
            else if (proxy_url_.find("socks4://") == 0)
            {
                curl_easy_setopt(curl_, CURLOPT_PROXYTYPE, CURLPROXY_SOCKS4);
            }
            else
            {
                // HTTP/HTTPS proxy
                curl_easy_setopt(curl_, CURLOPT_HTTPPROXYTUNNEL, 1L); // Use CONNECT for HTTPS
                curl_easy_setopt(curl_, CURLOPT_PROXYTYPE, CURLPROXY_HTTP);
            }

            // Skip SSL verification when using proxy (residential proxies may intercept)
            curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYPEER, 0L);
            curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYHOST, 0L);
            curl_easy_setopt(curl_, CURLOPT_PROXY_SSL_VERIFYPEER, 0L);
            curl_easy_setopt(curl_, CURLOPT_PROXY_SSL_VERIFYHOST, 0L);
        }
    }

    void HttpClient::set_user_agent(const std::string &user_agent)
    {
        options_.user_agent = user_agent;
        if (curl_)
        {
            curl_easy_setopt(curl_, CURLOPT_USERAGENT, user_agent.empty() ? nullptr : user_agent.c_str());
        }
    }

    void HttpClient::set_dns_cache_timeout(long seconds)
    {
        options_.dns_cache_timeout_seconds = seconds;
        apply_options();
    }

    void HttpClient::set_keepalive_interval(long seconds)
    {
        options_.tcp_keepidle_seconds = seconds;
        options_.tcp_keepintvl_seconds = seconds;
        apply_options();
    }

    void HttpClient::configure(const HttpClientOptions &options)
    {
        options_ = options;
        proxy_url_ = options.proxy_url;
        apply_options();
    }

    void HttpClient::apply_options()
    {
        if (!curl_)
            return;

        curl_easy_setopt(curl_, CURLOPT_TIMEOUT_MS, options_.timeout_ms);
        curl_easy_setopt(curl_, CURLOPT_CONNECTTIMEOUT_MS, options_.connect_timeout_ms);
        curl_easy_setopt(curl_, CURLOPT_DNS_CACHE_TIMEOUT, options_.dns_cache_timeout_seconds);
        curl_easy_setopt(curl_, CURLOPT_TCP_NODELAY, options_.tcp_nodelay ? 1L : 0L);
        curl_easy_setopt(curl_, CURLOPT_TCP_KEEPALIVE, options_.tcp_keepalive ? 1L : 0L);
        curl_easy_setopt(curl_, CURLOPT_TCP_KEEPIDLE, options_.tcp_keepidle_seconds);
        curl_easy_setopt(curl_, CURLOPT_TCP_KEEPINTVL, options_.tcp_keepintvl_seconds);
        curl_easy_setopt(curl_, CURLOPT_FORBID_REUSE, options_.allow_connection_reuse ? 0L : 1L);
        curl_easy_setopt(curl_, CURLOPT_FRESH_CONNECT, options_.allow_connection_reuse ? 0L : 1L);
        curl_easy_setopt(curl_, CURLOPT_USERAGENT, options_.user_agent.empty() ? nullptr : options_.user_agent.c_str());
        set_proxy(options_.proxy_url);
    }

    size_t HttpClient::write_callback(char *ptr, size_t size, size_t nmemb, void *userdata)
    {
        auto *response = static_cast<std::string *>(userdata);
        size_t total_size = size * nmemb;
        response->append(ptr, total_size);
        return total_size;
    }

    HttpResponse HttpClient::perform(const std::string &method, const std::string &path, const std::string &url)
    {
        HttpResponse response;
        response.status_code = 0;
        response.elapsed_ms = 0.0;

        auto start = std::chrono::high_resolution_clock::now();

        curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headers_);
        curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response.body);

        CURLcode res = curl_easy_perform(curl_);

        auto end = std::chrono::high_resolution_clock::now();
        response.elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();

        long num_connects = 0;
        curl_easy_getinfo(curl_, CURLINFO_NUM_CONNECTS, &num_connects);
        curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &response.status_code);
        response.error = res == CURLE_OK ? "" : curl_easy_strerror(res);

        // Track connection reuse stats
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            total_requests_++;
            total_latency_ms_ += response.elapsed_ms;
            last_latency_ms_ = response.elapsed_ms;
            min_latency_ms_ = total_requests_ == 1 ? response.elapsed_ms : std::min(min_latency_ms_, response.elapsed_ms);
            max_latency_ms_ = std::max(max_latency_ms_, response.elapsed_ms);
            bytes_received_ += static_cast<long>(response.body.size());
            status_counts_[response.status_code]++;
            if (res != CURLE_OK)
            {
                curl_errors_++;
            }

            // Check if connection was reused
            bool reused_connection = num_connects == 0;
            if (reused_connection)
            {
                reused_connections_++;
            }
            last_metrics_.method = method;
            last_metrics_.path = path;
            last_metrics_.status_code = response.status_code;
            last_metrics_.elapsed_ms = response.elapsed_ms;
            last_metrics_.bytes_received = static_cast<long>(response.body.size());
            last_metrics_.curl_code = static_cast<int>(res);
            last_metrics_.reused_connection = reused_connection;
        }

        return response;
    }

    HttpResponse HttpClient::get(const std::string &path)
    {
        std::string url = base_url_.empty() ? path : base_url_ + path;

        curl_easy_setopt(curl_, CURLOPT_HTTPGET, 1L);
        curl_easy_setopt(curl_, CURLOPT_POST, 0L);

        return perform("GET", path, url);
    }

    HttpResponse HttpClient::get(const std::string &path, const std::map<std::string, std::string> &custom_headers)
    {
        // Save original headers
        struct curl_slist *original_headers = headers_;

        // Add custom headers
        struct curl_slist *temp_headers = nullptr;
        for (auto h = headers_; h; h = h->next)
        {
            temp_headers = curl_slist_append(temp_headers, h->data);
        }
        for (const auto &[key, value] : custom_headers)
        {
            std::string header = key + ": " + value;
            temp_headers = curl_slist_append(temp_headers, header.c_str());
        }
        headers_ = temp_headers;

        auto response = get(path);

        // Restore original headers
        curl_slist_free_all(headers_);
        headers_ = original_headers;

        return response;
    }

    HttpResponse HttpClient::post(const std::string &path, const std::string &body)
    {
        std::string url = base_url_.empty() ? path : base_url_ + path;

        curl_easy_setopt(curl_, CURLOPT_POST, 1L);
        curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(curl_, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));

        return perform("POST", path, url);
    }

    HttpResponse HttpClient::post(const std::string &path, const std::string &body, const std::map<std::string, std::string> &custom_headers)
    {
        // Save original headers
        struct curl_slist *original_headers = headers_;

        // Add custom headers
        struct curl_slist *temp_headers = nullptr;
        for (auto h = headers_; h; h = h->next)
        {
            temp_headers = curl_slist_append(temp_headers, h->data);
        }
        for (const auto &[key, value] : custom_headers)
        {
            std::string header = key + ": " + value;
            temp_headers = curl_slist_append(temp_headers, header.c_str());
        }
        headers_ = temp_headers;

        auto response = post(path, body);

        // Restore original headers
        curl_slist_free_all(headers_);
        headers_ = original_headers;

        return response;
    }

    HttpResponse HttpClient::del(const std::string &path, const std::string &body)
    {
        std::string url = base_url_.empty() ? path : base_url_ + path;

        curl_easy_setopt(curl_, CURLOPT_CUSTOMREQUEST, "DELETE");
        if (!body.empty())
        {
            curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, body.c_str());
            curl_easy_setopt(curl_, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
        }

        auto response = perform("DELETE", path, url);

        // Reset to default
        curl_easy_setopt(curl_, CURLOPT_CUSTOMREQUEST, nullptr);

        return response;
    }

    HttpResponse HttpClient::del(const std::string &path, const std::string &body, const std::map<std::string, std::string> &custom_headers)
    {
        // Save original headers
        struct curl_slist *original_headers = headers_;

        // Add custom headers
        struct curl_slist *temp_headers = nullptr;
        for (auto h = headers_; h; h = h->next)
        {
            temp_headers = curl_slist_append(temp_headers, h->data);
        }
        for (const auto &[key, value] : custom_headers)
        {
            std::string header = key + ": " + value;
            temp_headers = curl_slist_append(temp_headers, header.c_str());
        }
        headers_ = temp_headers;

        auto response = del(path, body);

        // Restore original headers
        curl_slist_free_all(headers_);
        headers_ = original_headers;

        return response;
    }

    // ============================================================
    // Connection Warming and Heartbeat
    // ============================================================

    bool HttpClient::warm_connection()
    {
        if (base_url_.empty())
        {
            return false;
        }

        // Hit a cheap endpoint to establish TCP/TLS
        auto response = get("/");
        if (response.ok() || response.status_code == 404)
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            connection_warm_ = true;
            return true;
        }
        return false;
    }

    void HttpClient::start_heartbeat(long interval_seconds)
    {
        if (heartbeat_running_.load())
        {
            return; // Already running
        }

        heartbeat_running_.store(true);
        heartbeat_thread_ = std::thread([this, interval_seconds]()
                                        {
            while (heartbeat_running_.load())
            {
                // Sleep in small increments to allow quick shutdown
                for (long i = 0; i < interval_seconds * 10 && heartbeat_running_.load(); ++i)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }

                if (!heartbeat_running_.load())
                {
                    break;
                }

                // Send a lightweight GET to keep connection alive
                std::lock_guard<std::mutex> lock(curl_mutex_);
                if (curl_ && !base_url_.empty())
                {
                    get("/");
                }
            } });
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
        ConnectionStats stats;
        stats.total_requests = total_requests_;
        stats.reused_connections = reused_connections_;
        stats.curl_errors = curl_errors_;
        stats.bytes_received = bytes_received_;
        stats.status_counts = status_counts_;
        stats.avg_latency_ms = total_requests_ > 0 ? total_latency_ms_ / total_requests_ : 0.0;
        stats.last_latency_ms = last_latency_ms_;
        stats.min_latency_ms = min_latency_ms_;
        stats.max_latency_ms = max_latency_ms_;
        stats.connection_warm = connection_warm_;
        return stats;
    }

    RequestMetrics HttpClient::get_last_request_metrics() const
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        return last_metrics_;
    }

} // namespace polymarket
