#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <ixwebsocket/IXGetFreePort.h>
#include <ixwebsocket/IXWebSocketServer.h>

namespace websocket_test
{
    class LocalWebSocketServer
    {
    public:
        // first_handshake_delay stalls the first accepted connection before
        // the WebSocket handshake, simulating a slow upgrade response.
        explicit LocalWebSocketServer(
            std::chrono::milliseconds first_handshake_delay = std::chrono::milliseconds(0))
            : port_(ix::getFreePort()), server_(port_, "127.0.0.1")
        {
            if (first_handshake_delay.count() > 0)
            {
                server_.setConnectionStateFactory(
                    [this, first_handshake_delay]
                    {
                        if (!handshake_delayed_)
                        {
                            handshake_delayed_ = true;
                            std::this_thread::sleep_for(first_handshake_delay);
                        }
                        return ix::ConnectionState::createConnectionState();
                    });
            }

            server_.setOnClientMessageCallback(
                [this](std::shared_ptr<ix::ConnectionState>,
                       ix::WebSocket &,
                       const ix::WebSocketMessagePtr &message)
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    if (message->type == ix::WebSocketMessageType::Open)
                    {
                        ++connections_;
                        cv_.notify_all();
                    }
                    else if (message->type == ix::WebSocketMessageType::Message)
                    {
                        received_.push_back(message->str);
                        cv_.notify_all();
                    }
                });

            const auto listen_result = server_.listen();
            if (!listen_result.first)
            {
                throw std::runtime_error(listen_result.second);
            }
            server_.start();
        }

        ~LocalWebSocketServer()
        {
            if (!wait_for_no_clients(std::chrono::seconds(5)))
            {
                std::fputs("Local WebSocket server clients did not stop\n", stderr);
                std::abort();
            }
            server_.stop();
        }

        std::string url() const
        {
            return "ws://127.0.0.1:" + std::to_string(port_);
        }

        bool send_to_clients(const std::string &message)
        {
            bool sent = false;
            for (const auto &client : server_.getClients())
            {
                sent = client->send(message).success || sent;
            }
            return sent;
        }

        void close_clients()
        {
            for (const auto &client : server_.getClients())
            {
                client->close();
            }
        }

        void close_clients(uint16_t code, const std::string &reason)
        {
            for (const auto &client : server_.getClients())
            {
                client->close(code, reason);
            }
        }

        bool wait_for_connections(std::size_t count,
                                  std::chrono::milliseconds timeout)
        {
            std::unique_lock<std::mutex> lock(mutex_);
            return cv_.wait_for(lock, timeout, [this, count]
                                { return connections_ >= count; });
        }

        bool wait_for_message_count(const std::string &message,
                                    std::size_t count,
                                    std::chrono::milliseconds timeout)
        {
            std::unique_lock<std::mutex> lock(mutex_);
            return cv_.wait_for(lock, timeout, [this, &message, count]
                                {
                                    std::size_t matches = 0;
                                    for (const auto &received : received_)
                                    {
                                        matches += received == message ? 1U : 0U;
                                    }
                                    return matches >= count;
                                });
        }

        bool wait_for_no_clients(std::chrono::milliseconds timeout)
        {
            const auto deadline = std::chrono::steady_clock::now() + timeout;
            while (std::chrono::steady_clock::now() < deadline)
            {
                if (server_.getClients().empty()) return true;
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            return server_.getClients().empty();
        }

    private:
        int port_;
        ix::WebSocketServer server_;
        std::mutex mutex_;
        std::condition_variable cv_;
        std::size_t connections_{0};
        bool handshake_delayed_{false}; // accept thread only
        std::vector<std::string> received_;
    };
}
