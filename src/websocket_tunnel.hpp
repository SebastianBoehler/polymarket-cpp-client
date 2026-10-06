#pragma once

#include "polymarket/network.hpp"

#include <curl/curl.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace polymarket::detail
{
    // IXWebSocket cannot use proxies or bind interfaces. When a route is set,
    // WebSocketClient points IXWebSocket at this loopback relay instead. The
    // relay forwards each accepted connection over a libcurl CONNECT_ONLY
    // connection, which performs the proxy handshake and the TLS session with
    // the real host, so WebSockets get the same route as REST requests.
    //
    // IXWebSocket speaks plain ws:// over loopback; the remote leg keeps the
    // original scheme and certificate verification. One connection is relayed
    // at a time, matching one IXWebSocket per WebSocketClient.
    class WebSocketTunnel
    {
      public:
        WebSocketTunnel(const std::string &remote_url, NetworkRoute route);
        ~WebSocketTunnel();

        WebSocketTunnel(const WebSocketTunnel &) = delete;
        WebSocketTunnel &operator=(const WebSocketTunnel &) = delete;

        const std::string &local_url() const { return local_url_; }
        // Host and Origin values IXWebSocket would send to the remote directly.
        const std::string &host_header() const { return host_header_; }
        const std::string &origin_header() const { return origin_header_; }
        std::string last_error() const;

      private:
        void run();
        void serve(int client_fd);
        CURL *open_upstream();
        void relay(int client_fd, CURL *upstream, curl_socket_t upstream_fd);
        void set_error(std::string message);
        static int abort_on_stop(void *tunnel, curl_off_t, curl_off_t, curl_off_t, curl_off_t);

        NetworkRoute route_;
        std::string upstream_url_;
        std::string local_url_;
        std::string host_header_;
        std::string origin_header_;
        int listen_fd_{-1};
        int wake_read_fd_{-1};
        int wake_write_fd_{-1};
        bool global_acquired_{false};
        std::atomic<bool> stopping_{false};
        mutable std::mutex error_mutex_;
        std::string last_error_;
        std::thread worker_;
    };
} // namespace polymarket::detail
