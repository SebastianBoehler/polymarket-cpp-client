#include "http_client.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>

namespace polymarket
{
    namespace
    {
        class ForegroundWaiter
        {
        public:
            explicit ForegroundWaiter(std::atomic<uint32_t> &waiters)
                : waiters_(waiters)
            {
                waiters_.fetch_add(1);
            }

            ~ForegroundWaiter()
            {
                if (pending_)
                    waiters_.fetch_sub(1);
            }

            void acquired()
            {
                waiters_.fetch_sub(1);
                pending_ = false;
            }

        private:
            std::atomic<uint32_t> &waiters_;
            bool pending_{true};
        };
    }

    size_t HttpClient::write_callback(char *ptr, size_t size, size_t nmemb, void *userdata)
    {
        auto *response = static_cast<std::string *>(userdata);
        const size_t total_size = size * nmemb;
        response->append(ptr, total_size);
        return total_size;
    }

    size_t HttpClient::header_callback(char *ptr, size_t size, size_t nmemb, void *userdata)
    {
        auto *headers = static_cast<std::map<std::string, std::string> *>(userdata);
        const size_t total_size = size * nmemb;
        std::string line(ptr, total_size);
        const auto colon = line.find(':');
        if (colon == std::string::npos)
        {
            return total_size;
        }

        auto key = line.substr(0, colon);
        auto value = line.substr(colon + 1);
        auto trim = [](std::string &text)
        {
            while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
            {
                text.erase(text.begin());
            }
            while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
            {
                text.pop_back();
            }
        };
        trim(key);
        trim(value);
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c)
                       { return static_cast<char>(std::tolower(c)); });
        if (!key.empty())
        {
            (*headers)[key] = value;
        }
        return total_size;
    }

    HttpResponse HttpClient::perform(const std::string &method, const std::string &path, const std::string &url)
    {
        HttpResponse response{0, "", "", 0.0, {}};
        const auto start = std::chrono::high_resolution_clock::now();

        curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headers_);
        curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response.body);
        curl_easy_setopt(curl_, CURLOPT_HEADERFUNCTION, header_callback);
        curl_easy_setopt(curl_, CURLOPT_HEADERDATA, &response.headers);

        const CURLcode result = curl_easy_perform(curl_);
        const auto end = std::chrono::high_resolution_clock::now();
        response.elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();

        long num_connects = 0;
        curl_easy_getinfo(curl_, CURLINFO_NUM_CONNECTS, &num_connects);
        curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &response.status_code);
        response.error = result == CURLE_OK ? "" : curl_easy_strerror(result);

        std::lock_guard<std::mutex> lock(stats_mutex_);
        ++total_requests_;
        total_latency_ms_ += response.elapsed_ms;
        last_latency_ms_ = response.elapsed_ms;
        min_latency_ms_ = total_requests_ == 1 ? response.elapsed_ms : std::min(min_latency_ms_, response.elapsed_ms);
        max_latency_ms_ = std::max(max_latency_ms_, response.elapsed_ms);
        bytes_received_ += static_cast<long>(response.body.size());
        ++status_counts_[response.status_code];
        if (result != CURLE_OK)
        {
            ++curl_errors_;
        }

        const bool reused_connection = num_connects == 0;
        if (reused_connection)
        {
            ++reused_connections_;
        }
        last_metrics_ = {method, path, response.status_code, response.elapsed_ms,
                         static_cast<long>(response.body.size()), static_cast<int>(result), reused_connection};
        return response;
    }

    HttpResponse HttpClient::get(const std::string &path)
    {
        ForegroundWaiter waiter(foreground_waiters_);
        std::lock_guard<std::recursive_mutex> lock(curl_mutex_);
        waiter.acquired();
        const std::string url = base_url_.empty() ? path : base_url_ + path;
        curl_easy_setopt(curl_, CURLOPT_HTTPGET, 1L);
        curl_easy_setopt(curl_, CURLOPT_POST, 0L);
        return perform("GET", path, url);
    }

    HttpResponse HttpClient::get(const std::string &path, const std::map<std::string, std::string> &custom_headers)
    {
        ForegroundWaiter waiter(foreground_waiters_);
        std::lock_guard<std::recursive_mutex> lock(curl_mutex_);
        waiter.acquired();
        struct curl_slist *original_headers = headers_;
        struct curl_slist *temp_headers = nullptr;
        for (auto header = headers_; header; header = header->next)
        {
            temp_headers = curl_slist_append(temp_headers, header->data);
        }
        for (const auto &[key, value] : custom_headers)
        {
            temp_headers = curl_slist_append(temp_headers, (key + ": " + value).c_str());
        }
        headers_ = temp_headers;
        auto response = get(path);
        curl_slist_free_all(headers_);
        headers_ = original_headers;
        return response;
    }

    HttpResponse HttpClient::post(const std::string &path, const std::string &body)
    {
        ForegroundWaiter waiter(foreground_waiters_);
        std::lock_guard<std::recursive_mutex> lock(curl_mutex_);
        waiter.acquired();
        const std::string url = base_url_.empty() ? path : base_url_ + path;
        curl_easy_setopt(curl_, CURLOPT_POST, 1L);
        curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(curl_, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
        auto response = perform("POST", path, url);

        curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, nullptr);
        curl_easy_setopt(curl_, CURLOPT_POSTFIELDSIZE, 0L);
        curl_easy_setopt(curl_, CURLOPT_POST, 0L);
        return response;
    }

    HttpResponse HttpClient::post(const std::string &path, const std::string &body,
                                  const std::map<std::string, std::string> &custom_headers)
    {
        ForegroundWaiter waiter(foreground_waiters_);
        std::lock_guard<std::recursive_mutex> lock(curl_mutex_);
        waiter.acquired();
        struct curl_slist *original_headers = headers_;
        struct curl_slist *temp_headers = nullptr;
        for (auto header = headers_; header; header = header->next)
        {
            temp_headers = curl_slist_append(temp_headers, header->data);
        }
        for (const auto &[key, value] : custom_headers)
        {
            temp_headers = curl_slist_append(temp_headers, (key + ": " + value).c_str());
        }
        headers_ = temp_headers;
        auto response = post(path, body);
        curl_slist_free_all(headers_);
        headers_ = original_headers;
        return response;
    }

    HttpResponse HttpClient::del(const std::string &path, const std::string &body)
    {
        ForegroundWaiter waiter(foreground_waiters_);
        std::lock_guard<std::recursive_mutex> lock(curl_mutex_);
        waiter.acquired();
        const std::string url = base_url_.empty() ? path : base_url_ + path;
        curl_easy_setopt(curl_, CURLOPT_CUSTOMREQUEST, "DELETE");
        curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, body.empty() ? nullptr : body.c_str());
        curl_easy_setopt(curl_, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
        auto response = perform("DELETE", path, url);

        curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, nullptr);
        curl_easy_setopt(curl_, CURLOPT_POSTFIELDSIZE, 0L);
        curl_easy_setopt(curl_, CURLOPT_POST, 0L);
        curl_easy_setopt(curl_, CURLOPT_CUSTOMREQUEST, nullptr);
        return response;
    }

    HttpResponse HttpClient::del(const std::string &path, const std::string &body,
                                 const std::map<std::string, std::string> &custom_headers)
    {
        ForegroundWaiter waiter(foreground_waiters_);
        std::lock_guard<std::recursive_mutex> lock(curl_mutex_);
        waiter.acquired();
        struct curl_slist *original_headers = headers_;
        struct curl_slist *temp_headers = nullptr;
        for (auto header = headers_; header; header = header->next)
        {
            temp_headers = curl_slist_append(temp_headers, header->data);
        }
        for (const auto &[key, value] : custom_headers)
        {
            temp_headers = curl_slist_append(temp_headers, (key + ": " + value).c_str());
        }
        headers_ = temp_headers;
        auto response = del(path, body);
        curl_slist_free_all(headers_);
        headers_ = original_headers;
        return response;
    }
} // namespace polymarket
