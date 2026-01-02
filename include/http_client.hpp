#pragma once

#include <string>
#include <optional>
#include <functional>
#include <map>
#include <curl/curl.h>

namespace polymarket
{

    // HTTP response
    struct HttpResponse
    {
        long status_code;
        std::string body;
        std::string error;
        double elapsed_ms;

        bool ok() const { return status_code >= 200 && status_code < 300; }
    };

    // High-performance HTTP client using libcurl
    class HttpClient
    {
    public:
        HttpClient();
        ~HttpClient();

        // Disable copy
        HttpClient(const HttpClient &) = delete;
        HttpClient &operator=(const HttpClient &) = delete;

        // Enable move
        HttpClient(HttpClient &&other) noexcept;
        HttpClient &operator=(HttpClient &&other) noexcept;

        // Configuration
        void set_timeout_ms(long timeout_ms);
        void set_base_url(const std::string &base_url);
        void add_header(const std::string &header);

        // HTTP methods
        HttpResponse get(const std::string &path);
        HttpResponse get(const std::string &path, const std::map<std::string, std::string> &custom_headers);
        HttpResponse post(const std::string &path, const std::string &body);
        HttpResponse post(const std::string &path, const std::string &body, const std::map<std::string, std::string> &custom_headers);
        HttpResponse del(const std::string &path, const std::string &body = "");
        HttpResponse del(const std::string &path, const std::string &body, const std::map<std::string, std::string> &custom_headers);

    private:
        CURL *curl_;
        struct curl_slist *headers_;
        std::string base_url_;
        long timeout_ms_;

        void init();
        void cleanup();
        HttpResponse perform(const std::string &url);

        static size_t write_callback(char *ptr, size_t size, size_t nmemb, void *userdata);
    };

    // Global initialization (call once at startup)
    void http_global_init();
    void http_global_cleanup();

} // namespace polymarket
