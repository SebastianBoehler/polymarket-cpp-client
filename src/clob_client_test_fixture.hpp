#pragma once

#include "clob_client.hpp"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <atomic>
#include <chrono>
#include <cctype>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace clob_test
{
    using namespace polymarket;
    struct Request
    {
        std::string method;
        std::string target;
        std::string body;
        std::map<std::string, std::string> headers;
    };

    struct QueuedResponse
    {
        int status;
        std::string body;
        int delay_ms{0};
        std::function<std::string(const Request &)> body_factory;
    };

    class LocalServer
    {
    public:
        LocalServer()
        {
            fd_ = socket(AF_INET, SOCK_STREAM, 0);
            if (fd_ < 0)
                throw std::runtime_error("socket failed");
            int yes = 1;
            setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            if (bind(fd_, reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0 || listen(fd_, 8) < 0)
                throw std::runtime_error("server setup failed");
            socklen_t size = sizeof(address);
            getsockname(fd_, reinterpret_cast<sockaddr *>(&address), &size);
            port_ = ntohs(address.sin_port);
            worker_ = std::thread([this] { serve(); });
        }

        ~LocalServer()
        {
            running_.store(false);
            shutdown(fd_, SHUT_RDWR);
            close(fd_);
            if (worker_.joinable())
                worker_.join();
        }

        std::string url() const { return "http://127.0.0.1:" + std::to_string(port_); }

        void enqueue(std::string body, int status = 200, int delay_ms = 0)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            responses_.push_back({status, std::move(body), delay_ms, {}});
        }

        void enqueue(std::function<std::string(const Request &)> body_factory,
                     int status = 200, int delay_ms = 0)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            responses_.push_back({status, {}, delay_ms, std::move(body_factory)});
        }

        std::vector<Request> requests() const
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return requests_;
        }

    private:
        int fd_{-1};
        int port_{0};
        std::atomic<bool> running_{true};
        std::thread worker_;
        mutable std::mutex mutex_;
        std::vector<QueuedResponse> responses_;
        std::vector<Request> requests_;

        static Request read_request(int client)
        {
            std::string raw;
            char buffer[4096];
            while (raw.find("\r\n\r\n") == std::string::npos)
            {
                const auto count = recv(client, buffer, sizeof(buffer), 0);
                if (count <= 0)
                    break;
                raw.append(buffer, static_cast<size_t>(count));
            }
            const auto header_end = raw.find("\r\n\r\n");
            std::istringstream headers(raw.substr(0, header_end));
            Request request;
            std::string version;
            headers >> request.method >> request.target >> version;
            std::string line;
            std::getline(headers, line);
            size_t content_length = 0;
            while (std::getline(headers, line))
            {
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                const auto colon = line.find(':');
                if (colon == std::string::npos)
                    continue;
                auto key = line.substr(0, colon);
                for (auto &c : key)
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                auto value = line.substr(colon + 1);
                value.erase(0, value.find_first_not_of(' '));
                request.headers[key] = value;
                if (key == "content-length")
                    content_length = static_cast<size_t>(std::stoul(value));
            }
            const size_t body_start = header_end == std::string::npos ? raw.size() : header_end + 4;
            while (raw.size() < body_start + content_length)
            {
                const auto count = recv(client, buffer, sizeof(buffer), 0);
                if (count <= 0)
                    break;
                raw.append(buffer, static_cast<size_t>(count));
            }
            request.body = raw.substr(body_start, content_length);
            return request;
        }

        void serve()
        {
            while (running_.load())
            {
                const int client = accept(fd_, nullptr, nullptr);
                if (client < 0)
                    continue;
                const auto request = read_request(client);
                QueuedResponse response{500, R"({"error":"missing fixture"})", 0, {}};
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    requests_.push_back(request);
                    if (!responses_.empty())
                    {
                        response = std::move(responses_.front());
                        responses_.erase(responses_.begin());
                    }
                }
                if (response.body_factory)
                    response.body = response.body_factory(request);
                if (response.delay_ms > 0)
                    std::this_thread::sleep_for(std::chrono::milliseconds(response.delay_ms));
                const std::string reason = response.status == 200 ? "OK" : "ERROR";
                const std::string wire = "HTTP/1.1 " + std::to_string(response.status) + " " + reason +
                                         "\r\nContent-Type: application/json\r\nContent-Length: " +
                                         std::to_string(response.body.size()) + "\r\nConnection: close\r\n\r\n" + response.body;
                send(client, wire.data(), wire.size(), 0);
                close(client);
            }
        }
    };

    inline bool check(bool condition, const std::string &message)
    {
        if (!condition)
            std::cerr << message << '\n';
        return condition;
    }

    inline std::string expected_signature(const Request &request,
                                          const std::string &signed_path,
                                          const std::string &body = "")
    {
        const std::string message = request.headers.at("poly_timestamp") + request.method + signed_path + body;
        unsigned char digest[EVP_MAX_MD_SIZE];
        unsigned int digest_size = 0;
        HMAC(EVP_sha256(), "secret", 6,
             reinterpret_cast<const unsigned char *>(message.data()), message.size(),
             digest, &digest_size);
        unsigned char encoded[64]{};
        const int encoded_size = EVP_EncodeBlock(encoded, digest, static_cast<int>(digest_size));
        std::string result(reinterpret_cast<char *>(encoded), static_cast<size_t>(encoded_size));
        for (auto &c : result)
        {
            if (c == '+')
                c = '-';
            else if (c == '/')
                c = '_';
        }
        return result;
    }

    inline ClobClient authenticated_client(const std::string &url)
    {
        ApiCredentials credentials{"test-key", "c2VjcmV0", "test-pass"};
        return ClobClient(url, 137,
                          "0x0000000000000000000000000000000000000000000000000000000000000001",
                          credentials);
    }

    bool test_market_order_depth_and_fail_closed_metadata();
    bool test_market_order_rejects_resting_types();
    bool test_limit_order_fail_closed_metadata();
    bool test_public_order_price_grid_validation();
    bool test_metadata_cache_avoids_repeated_round_trips();
    bool test_metadata_cache_coalesces_concurrent_cold_misses();
    bool test_metadata_cache_invalidation_refreshes_values();
    bool test_signer_only_l1_bootstrap_installs_credentials();
    bool test_clob_constructors_reject_unsupported_chains();
    bool test_clob_constructors_require_non_eoa_funder();
    bool test_clob_warm_connection_uses_time_only();
    bool test_order_result_schema_failures();
    bool test_order_post_response_contracts();
    bool test_cancellation_response_contracts();
    bool test_conditional_balance_allowance_token();
    bool test_rewards_contracts();
    bool test_rewards_schemas_fail_closed_atomically();
    bool test_scoring_and_notifications();
    bool test_credentials_validation();
    bool test_clob_type_scalar_defaults();
    bool test_pagination_cycle_and_page_limit_guards();
}
