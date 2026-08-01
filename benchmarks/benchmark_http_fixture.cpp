#include "http_client.hpp"
#include <arpa/inet.h>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

using namespace polymarket;

namespace
{
    class LocalHttpFixture
    {
    public:
        LocalHttpFixture()
        {
            server_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
            int opt = 1;
            ::setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            address.sin_port = 0;
            if (::bind(server_fd_, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0)
            {
                throw std::runtime_error(std::strerror(errno));
            }
            if (::listen(server_fd_, 64) != 0)
            {
                throw std::runtime_error(std::strerror(errno));
            }

            socklen_t len = sizeof(address);
            ::getsockname(server_fd_, reinterpret_cast<sockaddr *>(&address), &len);
            port_ = ntohs(address.sin_port);
            thread_ = std::thread([this]()
                                  { serve(); });
        }

        ~LocalHttpFixture()
        {
            running_.store(false);
            ::shutdown(server_fd_, SHUT_RDWR);
            ::close(server_fd_);
            if (thread_.joinable())
            {
                thread_.join();
            }
        }

        std::string base_url() const
        {
            return "http://127.0.0.1:" + std::to_string(port_);
        }

        long accepted_connections() const
        {
            return accepted_connections_.load();
        }

    private:
        int server_fd_{-1};
        int port_{0};
        std::atomic<bool> running_{true};
        std::atomic<long> accepted_connections_{0};
        std::thread thread_;

        void serve()
        {
            while (running_.load())
            {
                int client = ::accept(server_fd_, nullptr, nullptr);
                if (client < 0)
                {
                    continue;
                }
                accepted_connections_++;

                while (running_.load())
                {
                    char buffer[512];
                    const auto bytes = ::read(client, buffer, sizeof(buffer));
                    if (bytes <= 0)
                        break;

                    const std::string body = R"({"ok":true})";
                    const std::string response =
                        "HTTP/1.1 200 OK\r\n"
                        "Content-Type: application/json\r\n"
                        "Content-Length: " +
                        std::to_string(body.size()) +
                        "\r\nConnection: keep-alive\r\n\r\n" + body;
                    (void)::write(client, response.data(), response.size());
                }
                ::close(client);
            }
        }
    };

    long run_warm(const std::string &base_url, int iterations)
    {
        HttpClient client;
        client.set_base_url(base_url);
        auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < iterations; ++i)
        {
            auto response = client.get("/fixture");
            if (!response.ok())
            {
                return -1;
            }
        }
        auto end = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    }

    long run_cold(const std::string &base_url, int iterations)
    {
        auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < iterations; ++i)
        {
            HttpClient client;
            client.set_base_url(base_url);
            auto response = client.get("/fixture");
            if (!response.ok())
            {
                return -1;
            }
        }
        auto end = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    }
}

int main(int argc, char **argv)
{
    const int iterations = argc > 1 ? std::stoi(argv[1]) : 200;
    LocalHttpFixture fixture;

    const auto cold_us = run_cold(fixture.base_url(), iterations);
    const auto cold_connections = fixture.accepted_connections();
    const auto warm_us = run_warm(fixture.base_url(), iterations);
    const auto warm_connections = fixture.accepted_connections() - cold_connections;
    if (cold_us < 0 || warm_us < 0)
    {
        std::cerr << "fixture request failed\n";
        return 1;
    }
    if (warm_connections != 1)
    {
        std::cerr << "warm benchmark opened " << warm_connections
                  << " connections; expected one reused connection\n";
        return 1;
    }

    std::cout << "benchmark=http_fixture iterations=" << iterations
              << " cold_total_us=" << cold_us
              << " warm_total_us=" << warm_us
              << " cold_avg_us=" << static_cast<double>(cold_us) / iterations
              << " warm_avg_us=" << static_cast<double>(warm_us) / iterations
              << " warm_connections=" << warm_connections << "\n";
    return 0;
}
