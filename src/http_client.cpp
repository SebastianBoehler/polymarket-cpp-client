#include "http_client.hpp"
#include "http_global.hpp"
#include <stdexcept>
#include <utility>

namespace polymarket
{

    void http_global_init()
    {
        detail::acquire_manual_http_global();
    }

    void http_global_cleanup()
    {
        detail::release_manual_http_global();
    }

    HttpClient::HttpClient()
        : curl_(nullptr), headers_(nullptr), global_acquired_(false),
          heartbeat_running_(false),
          total_requests_(0), reused_connections_(0),
          curl_errors_(0), bytes_received_(0),
          total_latency_ms_(0.0), last_latency_ms_(0.0),
          min_latency_ms_(0.0), max_latency_ms_(0.0),
          connection_warm_(false)
    {
        detail::acquire_http_global();
        global_acquired_ = true;
        try
        {
            init();
        }
        catch (...)
        {
            cleanup();
            detail::release_http_global();
            global_acquired_ = false;
            throw;
        }
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
        if (global_acquired_)
        {
            detail::release_http_global();
        }
    }

    HttpClient::HttpClient(HttpClient &&other) noexcept
        : curl_(nullptr), headers_(nullptr), global_acquired_(false),
          heartbeat_running_(false),
          total_requests_(0), reused_connections_(0),
          curl_errors_(0), bytes_received_(0),
          total_latency_ms_(0.0), last_latency_ms_(0.0),
          min_latency_ms_(0.0), max_latency_ms_(0.0),
          connection_warm_(false)
    {
        other.stop_heartbeat();
        {
            std::lock_guard<std::recursive_mutex> lock(other.curl_mutex_);
            curl_ = std::exchange(other.curl_, nullptr);
            headers_ = std::exchange(other.headers_, nullptr);
            global_acquired_ = std::exchange(other.global_acquired_, false);
            base_url_ = std::move(other.base_url_);
            proxy_url_ = std::move(other.proxy_url_);
            options_ = std::move(other.options_);
        }
        std::lock_guard<std::mutex> lock(other.stats_mutex_);
        total_requests_ = std::exchange(other.total_requests_, 0);
        reused_connections_ = std::exchange(other.reused_connections_, 0);
        curl_errors_ = std::exchange(other.curl_errors_, 0);
        bytes_received_ = std::exchange(other.bytes_received_, 0);
        status_counts_ = std::move(other.status_counts_);
        total_latency_ms_ = std::exchange(other.total_latency_ms_, 0.0);
        last_latency_ms_ = std::exchange(other.last_latency_ms_, 0.0);
        min_latency_ms_ = std::exchange(other.min_latency_ms_, 0.0);
        max_latency_ms_ = std::exchange(other.max_latency_ms_, 0.0);
        connection_warm_ = std::exchange(other.connection_warm_, false);
        last_metrics_ = std::move(other.last_metrics_);
    }

    HttpClient &HttpClient::operator=(HttpClient &&other) noexcept
    {
        if (this != &other)
        {
            stop_heartbeat();
            other.stop_heartbeat();
            bool release_previous_global = false;
            {
                std::scoped_lock lock(curl_mutex_, other.curl_mutex_);
                cleanup();
                release_previous_global = std::exchange(global_acquired_, false);
                curl_ = std::exchange(other.curl_, nullptr);
                headers_ = std::exchange(other.headers_, nullptr);
                global_acquired_ = std::exchange(other.global_acquired_, false);
                base_url_ = std::move(other.base_url_);
                proxy_url_ = std::move(other.proxy_url_);
                options_ = std::move(other.options_);
            }
            if (release_previous_global)
            {
                detail::release_http_global();
            }
            std::scoped_lock lock(stats_mutex_, other.stats_mutex_);
            total_requests_ = std::exchange(other.total_requests_, 0);
            reused_connections_ = std::exchange(other.reused_connections_, 0);
            curl_errors_ = std::exchange(other.curl_errors_, 0);
            bytes_received_ = std::exchange(other.bytes_received_, 0);
            status_counts_ = std::move(other.status_counts_);
            total_latency_ms_ = std::exchange(other.total_latency_ms_, 0.0);
            last_latency_ms_ = std::exchange(other.last_latency_ms_, 0.0);
            min_latency_ms_ = std::exchange(other.min_latency_ms_, 0.0);
            max_latency_ms_ = std::exchange(other.max_latency_ms_, 0.0);
            connection_warm_ = std::exchange(other.connection_warm_, false);
            last_metrics_ = std::move(other.last_metrics_);
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

        // Authenticated requests carry secrets in custom POLY_* headers, which
        // libcurl may forward across redirect origins. Fail closed on 3xx.
        curl_easy_setopt(curl_, CURLOPT_FOLLOWLOCATION, 0L);
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
        std::lock_guard<std::recursive_mutex> lock(curl_mutex_);
        options_.timeout_ms = timeout_ms;
        apply_options();
    }

    void HttpClient::set_base_url(const std::string &base_url)
    {
        std::lock_guard<std::recursive_mutex> lock(curl_mutex_);
        base_url_ = base_url;
        // Remove trailing slash
        if (!base_url_.empty() && base_url_.back() == '/')
        {
            base_url_.pop_back();
        }
    }

    void HttpClient::add_header(const std::string &header)
    {
        std::lock_guard<std::recursive_mutex> lock(curl_mutex_);
        headers_ = curl_slist_append(headers_, header.c_str());
    }

    void HttpClient::set_proxy(const std::string &proxy_url)
    {
        std::lock_guard<std::recursive_mutex> lock(curl_mutex_);
        options_.proxy_url = proxy_url;
        proxy_url_ = proxy_url;
        if (curl_)
        {
            if (proxy_url_.empty())
            {
                curl_easy_setopt(curl_, CURLOPT_PROXY, nullptr);
                curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYPEER, 1L);
                curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYHOST, 2L);
                curl_easy_setopt(curl_, CURLOPT_PROXY_SSL_VERIFYPEER, 1L);
                curl_easy_setopt(curl_, CURLOPT_PROXY_SSL_VERIFYHOST, 2L);
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

            curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYPEER, 1L);
            curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYHOST, 2L);
            curl_easy_setopt(curl_, CURLOPT_PROXY_SSL_VERIFYPEER, 1L);
            curl_easy_setopt(curl_, CURLOPT_PROXY_SSL_VERIFYHOST, 2L);
        }
    }

    void HttpClient::set_user_agent(const std::string &user_agent)
    {
        std::lock_guard<std::recursive_mutex> lock(curl_mutex_);
        options_.user_agent = user_agent;
        if (curl_)
        {
            curl_easy_setopt(curl_, CURLOPT_USERAGENT, user_agent.empty() ? nullptr : user_agent.c_str());
        }
    }

    void HttpClient::set_dns_cache_timeout(long seconds)
    {
        std::lock_guard<std::recursive_mutex> lock(curl_mutex_);
        options_.dns_cache_timeout_seconds = seconds;
        apply_options();
    }

    void HttpClient::set_keepalive_interval(long seconds)
    {
        std::lock_guard<std::recursive_mutex> lock(curl_mutex_);
        options_.tcp_keepidle_seconds = seconds;
        options_.tcp_keepintvl_seconds = seconds;
        apply_options();
    }

    void HttpClient::configure(const HttpClientOptions &options)
    {
        std::lock_guard<std::recursive_mutex> lock(curl_mutex_);
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

} // namespace polymarket
