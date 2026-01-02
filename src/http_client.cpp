#include "http_client.hpp"
#include <chrono>
#include <stdexcept>

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
        : curl_(nullptr), headers_(nullptr), timeout_ms_(5000)
    {
        init();
    }

    HttpClient::~HttpClient()
    {
        cleanup();
    }

    HttpClient::HttpClient(HttpClient &&other) noexcept
        : curl_(other.curl_), headers_(other.headers_), base_url_(std::move(other.base_url_)), timeout_ms_(other.timeout_ms_)
    {
        other.curl_ = nullptr;
        other.headers_ = nullptr;
    }

    HttpClient &HttpClient::operator=(HttpClient &&other) noexcept
    {
        if (this != &other)
        {
            cleanup();
            curl_ = other.curl_;
            headers_ = other.headers_;
            base_url_ = std::move(other.base_url_);
            timeout_ms_ = other.timeout_ms_;
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

        // Set default options for performance
        curl_easy_setopt(curl_, CURLOPT_TCP_NODELAY, 1L);
        curl_easy_setopt(curl_, CURLOPT_TCP_KEEPALIVE, 1L);
        curl_easy_setopt(curl_, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl_, CURLOPT_MAXREDIRS, 3L);
        curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, write_callback);

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
        timeout_ms_ = timeout_ms;
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

    size_t HttpClient::write_callback(char *ptr, size_t size, size_t nmemb, void *userdata)
    {
        auto *response = static_cast<std::string *>(userdata);
        size_t total_size = size * nmemb;
        response->append(ptr, total_size);
        return total_size;
    }

    HttpResponse HttpClient::perform(const std::string &url)
    {
        HttpResponse response;
        response.status_code = 0;

        auto start = std::chrono::high_resolution_clock::now();

        curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headers_);
        curl_easy_setopt(curl_, CURLOPT_TIMEOUT_MS, timeout_ms_);
        curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response.body);

        CURLcode res = curl_easy_perform(curl_);

        auto end = std::chrono::high_resolution_clock::now();
        response.elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();

        if (res != CURLE_OK)
        {
            response.error = curl_easy_strerror(res);
            return response;
        }

        curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &response.status_code);

        return response;
    }

    HttpResponse HttpClient::get(const std::string &path)
    {
        std::string url = base_url_.empty() ? path : base_url_ + path;

        curl_easy_setopt(curl_, CURLOPT_HTTPGET, 1L);
        curl_easy_setopt(curl_, CURLOPT_POST, 0L);

        return perform(url);
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

        return perform(url);
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

        auto response = perform(url);

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

} // namespace polymarket
