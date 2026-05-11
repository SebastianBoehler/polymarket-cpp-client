#include "http_client.hpp"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

using namespace polymarket;

namespace
{
    class LocalHttpServer
    {
    public:
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
            shutdown(fd_, SHUT_RDWR);
            close(fd_);
            if (worker_.joinable())
                worker_.join();
        }

        int port() const { return port_; }

    private:
        int fd_{-1};
        int port_{0};
        std::atomic<bool> running_{true};
        std::thread worker_;

        void serve()
        {
            while (running_.load())
            {
                int client = accept(fd_, nullptr, nullptr);
                if (client < 0)
                    continue;

                char buffer[512];
                read(client, buffer, sizeof(buffer));
                std::string body = R"({"ok":true})";
                std::string response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
                                       std::to_string(body.size()) + "\r\nConnection: keep-alive\r\n\r\n" + body;
                send(client, response.data(), response.size(), 0);
                close(client);
            }
        }
    };

    bool check(bool condition, const std::string &message)
    {
        if (!condition)
            std::cerr << message << "\n";
        return condition;
    }
}

int main()
{
    http_global_init();
    LocalHttpServer server;

    HttpClientOptions options;
    options.timeout_ms = 1000;
    options.connect_timeout_ms = 500;
    options.dns_cache_timeout_seconds = 5;
    options.user_agent = "transport-test";

    HttpClient client(options);
    client.set_base_url("http://127.0.0.1:" + std::to_string(server.port()));

    auto response = client.get("/status");
    auto stats = client.get_stats();
    auto metrics = client.get_last_request_metrics();

    bool ok = true;
    ok &= check(response.ok(), "expected local response to be ok");
    ok &= check(response.body == R"({"ok":true})", "unexpected response body");
    ok &= check(stats.total_requests == 1, "expected one request");
    ok &= check(stats.bytes_received == static_cast<long>(response.body.size()), "expected byte count to match body");
    ok &= check(stats.status_counts[200] == 1, "expected one HTTP 200");
    ok &= check(stats.last_latency_ms > 0.0, "expected last latency");
    ok &= check(stats.min_latency_ms > 0.0, "expected min latency");
    ok &= check(stats.max_latency_ms >= stats.min_latency_ms, "expected max >= min");
    ok &= check(metrics.method == "GET", "expected GET metrics");
    ok &= check(metrics.path == "/status", "expected path metrics");
    ok &= check(metrics.status_code == 200, "expected status metrics");
    ok &= check(metrics.bytes_received == static_cast<long>(response.body.size()), "expected metric byte count");

    http_global_cleanup();
    return ok ? 0 : 1;
}
