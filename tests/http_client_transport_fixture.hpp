#pragma once

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
    class LocalHttpServer
    {
    public:
        struct Request
        {
            std::string method;
            std::string path;
            std::string body;
        };

        LocalHttpServer()
        {
            fd_ = socket(AF_INET, SOCK_STREAM, 0);
            if (fd_ < 0)
                throw std::runtime_error("socket failed");

            int yes = 1;
            setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            addr.sin_port = 0;
            if (bind(fd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0)
                throw std::runtime_error("bind failed");
            if (listen(fd_, 4) < 0)
                throw std::runtime_error("listen failed");

            socklen_t len = sizeof(addr);
            getsockname(fd_, reinterpret_cast<sockaddr *>(&addr), &len);
            port_ = ntohs(addr.sin_port);
            worker_ = std::thread([this] { serve(); });
        }

        ~LocalHttpServer()
        {
            running_.store(false);
            release_heartbeat_response();
            shutdown(fd_, SHUT_RDWR);
            close(fd_);
            if (worker_.joinable())
                worker_.join();
        }

        int port() const { return port_; }

        std::vector<Request> requests() const
        {
            std::lock_guard<std::mutex> lock(requests_mutex_);
            return requests_;
        }

        void hold_heartbeat_response()
        {
            std::lock_guard<std::mutex> lock(heartbeat_mutex_);
            heartbeat_started_ = false;
            heartbeat_released_ = false;
        }

        bool wait_for_heartbeat(std::chrono::milliseconds timeout)
        {
            std::unique_lock<std::mutex> lock(heartbeat_mutex_);
            return heartbeat_cv_.wait_for(lock, timeout, [this] { return heartbeat_started_; });
        }

        void release_heartbeat_response()
        {
            {
                std::lock_guard<std::mutex> lock(heartbeat_mutex_);
                heartbeat_released_ = true;
            }
            heartbeat_cv_.notify_all();
        }

        void truncate_next_response()
        {
            truncate_next_response_.store(true);
        }

    private:
        int fd_{-1};
        int port_{0};
        std::atomic<bool> running_{true};
        std::thread worker_;
        mutable std::mutex requests_mutex_;
        std::vector<Request> requests_;
        std::mutex heartbeat_mutex_;
        std::condition_variable heartbeat_cv_;
        bool heartbeat_started_{false};
        bool heartbeat_released_{true};
        std::atomic<bool> truncate_next_response_{false};

        static std::size_t content_length(const std::string &request)
        {
            constexpr const char *header = "Content-Length:";
            const auto start = request.find(header);
            if (start == std::string::npos)
                return 0;
            const auto value_start = request.find_first_not_of(' ', start + std::char_traits<char>::length(header));
            const auto value_end = request.find("\r\n", value_start);
            return static_cast<std::size_t>(std::stoul(request.substr(value_start, value_end - value_start)));
        }

        static Request parse_request(const std::string &request)
        {
            Request parsed;
            const auto first_space = request.find(' ');
            const auto second_space = request.find(' ', first_space + 1);
            parsed.method = request.substr(0, first_space);
            parsed.path = request.substr(first_space + 1, second_space - first_space - 1);
            const auto body_start = request.find("\r\n\r\n");
            if (body_start != std::string::npos)
                parsed.body = request.substr(body_start + 4);
            return parsed;
        }

        void serve()
        {
            while (running_.load())
            {
                int client = accept(fd_, nullptr, nullptr);
                if (client < 0)
                    continue;

                std::string request;
                char buffer[512];
                while (request.find("\r\n\r\n") == std::string::npos)
                {
                    const auto bytes = read(client, buffer, sizeof(buffer));
                    if (bytes <= 0)
                        break;
                    request.append(buffer, static_cast<std::size_t>(bytes));
                }
                const auto header_end = request.find("\r\n\r\n");
                const auto expected_size = header_end == std::string::npos
                                               ? request.size()
                                               : header_end + 4 + content_length(request);
                while (request.size() < expected_size)
                {
                    const auto bytes = read(client, buffer, sizeof(buffer));
                    if (bytes <= 0)
                        break;
                    request.append(buffer, static_cast<std::size_t>(bytes));
                }
                const auto parsed = parse_request(request);
                {
                    std::lock_guard<std::mutex> lock(requests_mutex_);
                    requests_.push_back(parsed);
                }
                bool peer_closed = false;
                if (parsed.path == "/")
                {
                    std::unique_lock<std::mutex> lock(heartbeat_mutex_);
                    heartbeat_started_ = true;
                    heartbeat_cv_.notify_all();
                    while (!heartbeat_released_ && running_.load())
                    {
                        heartbeat_cv_.wait_for(lock, std::chrono::milliseconds(5));
                        lock.unlock();
                        char probe = 0;
                        const auto peer = recv(client, &probe, 1, MSG_PEEK | MSG_DONTWAIT);
                        lock.lock();
                        if (peer == 0)
                        {
                            peer_closed = true;
                            break;
                        }
                    }
                }
                std::string body = R"({"ok":true})";
                std::string response;
                if (parsed.path == "/redirect")
                {
                    response = "HTTP/1.1 302 Found\r\nLocation: http://127.0.0.1:" +
                               std::to_string(port_) +
                               "/redirect-target\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
                }
                else
                {
                    const auto declared_size = body.size() +
                                               (truncate_next_response_.exchange(false) ? 1 : 0);
                    response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
                               std::to_string(declared_size) +
                               "\r\nConnection: keep-alive\r\n\r\n" + body;
                }
                if (!peer_closed)
                    send(client, response.data(), response.size(), 0);
                close(client);
            }
        }
    };

    inline bool check(bool condition, const std::string &message)
    {
        if (!condition)
            std::cerr << message << "\n";
        return condition;
    }
}
