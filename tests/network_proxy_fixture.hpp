#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace network_test
{
    // Loopback proxy speaking HTTP CONNECT and SOCKS5 (no auth). It records
    // each tunnel target and the first bytes the client sent through it, and
    // only dials 127.0.0.1 so tests never leave the machine.
    class LocalProxy
    {
      public:
        enum class Mode : std::uint8_t
        {
            Forward,
            Reject,
            Stall
        };

        explicit LocalProxy(Mode mode = Mode::Forward) : mode_(mode)
        {
            fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
            if (fd_ < 0) throw std::runtime_error("proxy socket failed");
            int yes = 1;
            ::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            socklen_t length = sizeof(address);
            if (::bind(fd_, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0 ||
                ::listen(fd_, 8) != 0 ||
                ::getsockname(fd_, reinterpret_cast<sockaddr *>(&address), &length) != 0)
                throw std::runtime_error("proxy listen failed");
            port_ = ntohs(address.sin_port);
            acceptor_ = std::thread([this] { accept_loop(); });
        }

        ~LocalProxy()
        {
            running_.store(false);
            ::shutdown(fd_, SHUT_RDWR);
            ::close(fd_);
            if (acceptor_.joinable()) acceptor_.join();
            std::vector<std::thread> sessions;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                sessions.swap(sessions_);
            }
            for (auto &session : sessions)
                if (session.joinable()) session.join();
        }

        int port() const { return port_; }
        std::string http_url() const { return "http://127.0.0.1:" + std::to_string(port_); }
        std::string socks5h_url() const { return "socks5h://127.0.0.1:" + std::to_string(port_); }

        std::vector<std::string> targets() const
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return targets_;
        }

        std::vector<std::string> first_payloads() const
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return first_payloads_;
        }

        bool wait_for_targets(std::size_t count, std::chrono::milliseconds timeout)
        {
            std::unique_lock<std::mutex> lock(mutex_);
            return cv_.wait_for(lock, timeout, [&] { return targets_.size() >= count; });
        }

      private:
        static bool read_exact(int fd, void *output, std::size_t size)
        {
            auto *bytes = static_cast<unsigned char *>(output);
            while (size > 0)
            {
                const auto received = ::recv(fd, bytes, size, 0);
                if (received <= 0) return false;
                bytes += received;
                size -= static_cast<std::size_t>(received);
            }
            return true;
        }

        static bool send_all(int fd, const void *input, std::size_t size)
        {
            const auto *bytes = static_cast<const char *>(input);
            while (size > 0)
            {
                const auto sent = ::send(fd, bytes, size, 0);
                if (sent <= 0) return false;
                bytes += sent;
                size -= static_cast<std::size_t>(sent);
            }
            return true;
        }

        static int dial_loopback(int port)
        {
            const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            address.sin_port = htons(static_cast<uint16_t>(port));
            if (fd >= 0 &&
                ::connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0)
                return fd;
            if (fd >= 0) ::close(fd);
            return -1;
        }

        void accept_loop()
        {
            while (running_.load())
            {
                const int client = ::accept(fd_, nullptr, nullptr);
                if (client < 0) continue;
                std::lock_guard<std::mutex> lock(mutex_);
                sessions_.emplace_back(
                    [this, client]
                    {
                        session(client);
                        ::close(client);
                    });
            }
        }

        void record_target(const std::string &target)
        {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                targets_.push_back(target);
            }
            cv_.notify_all();
        }

        // Returns the target port, or -1 after rejecting the client.
        int handshake(int client)
        {
            unsigned char first = 0;
            if (!read_exact(client, &first, 1)) return -1;
            if (first == 0x05) return socks5_handshake(client);

            std::string request(1, static_cast<char>(first));
            char byte = 0;
            while (request.find("\r\n\r\n") == std::string::npos && read_exact(client, &byte, 1))
                request.push_back(byte);
            const auto start = request.find(' ') + 1;
            const auto target = request.substr(start, request.find(' ', start) - start);
            record_target(target);
            if (mode_ == Mode::Stall) return stall(client);
            if (mode_ == Mode::Reject)
            {
                const std::string denied = "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n";
                send_all(client, denied.data(), denied.size());
                return -1;
            }
            const std::string established = "HTTP/1.1 200 Connection established\r\n\r\n";
            if (!send_all(client, established.data(), established.size())) return -1;
            return std::stoi(target.substr(target.rfind(':') + 1));
        }

        int socks5_handshake(int client)
        {
            unsigned char method_count = 0;
            unsigned char methods[255];
            if (!read_exact(client, &method_count, 1) || !read_exact(client, methods, method_count))
                return -1;
            const unsigned char no_auth[2] = {0x05, 0x00};
            unsigned char header[4];
            if (!send_all(client, no_auth, 2) || !read_exact(client, header, 4)) return -1;

            std::string host;
            if (header[3] == 0x03)
            {
                unsigned char length = 0;
                if (!read_exact(client, &length, 1)) return -1;
                host.resize(length);
                if (!read_exact(client, host.data(), length)) return -1;
            }
            else if (header[3] == 0x01)
            {
                unsigned char ip[4];
                if (!read_exact(client, ip, 4)) return -1;
                host = std::to_string(ip[0]) + "." + std::to_string(ip[1]) + "." +
                       std::to_string(ip[2]) + "." + std::to_string(ip[3]);
            }
            else
                return -1;
            unsigned char port_bytes[2];
            if (!read_exact(client, port_bytes, 2)) return -1;
            const int port = (port_bytes[0] << 8) | port_bytes[1];
            record_target(host + ":" + std::to_string(port));
            if (mode_ == Mode::Stall) return stall(client);
            const unsigned char reply_code = mode_ == Mode::Reject ? 0x02 : 0x00;
            const unsigned char reply[10] = {0x05, reply_code, 0x00, 0x01, 127, 0, 0, 1, 0, 0};
            if (!send_all(client, reply, sizeof(reply)) || mode_ == Mode::Reject) return -1;
            return port;
        }

        int stall(int client)
        {
            char byte = 0;
            while (running_.load())
            {
                pollfd fd{client, POLLIN, 0};
                if (::poll(&fd, 1, 20) > 0 && ::recv(client, &byte, 1, 0) <= 0) break;
            }
            return -1;
        }

        void session(int client)
        {
            const int port = handshake(client);
            if (port < 0) return;
            const int upstream = dial_loopback(port);
            if (upstream < 0) return;

            bool first_chunk = true;
            char buffer[16 * 1024];
            while (running_.load())
            {
                pollfd fds[2] = {{client, POLLIN, 0}, {upstream, POLLIN, 0}};
                if (::poll(fds, 2, 20) <= 0) continue;
                const auto pump = [&](int from, int to, bool record)
                {
                    const auto received = ::recv(from, buffer, sizeof(buffer), 0);
                    if (received <= 0) return false;
                    if (record)
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        first_payloads_.emplace_back(buffer, static_cast<std::size_t>(received));
                    }
                    return send_all(to, buffer, static_cast<std::size_t>(received));
                };
                if ((fds[0].revents & (POLLIN | POLLHUP | POLLERR)) != 0)
                {
                    if (!pump(client, upstream, first_chunk)) break;
                    first_chunk = false;
                }
                if ((fds[1].revents & (POLLIN | POLLHUP | POLLERR)) != 0 &&
                    !pump(upstream, client, false))
                    break;
            }
            ::close(upstream);
        }

        Mode mode_;
        int fd_{-1};
        int port_{0};
        std::atomic<bool> running_{true};
        std::thread acceptor_;
        mutable std::mutex mutex_;
        std::condition_variable cv_;
        std::vector<std::thread> sessions_;
        std::vector<std::string> targets_;
        std::vector<std::string> first_payloads_;
    };
} // namespace network_test
