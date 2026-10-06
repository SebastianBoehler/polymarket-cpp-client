#include "websocket_tunnel.hpp"

#include "http_global.hpp"
#include "network_route.hpp"

#include <ixwebsocket/IXUrlParser.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <utility>

namespace polymarket::detail
{
    namespace
    {
        constexpr long upstream_connect_timeout_ms = 10000;
        constexpr std::size_t relay_chunk_bytes = 16 * 1024;
        constexpr std::size_t max_pending_bytes = 1024 * 1024;
#ifdef MSG_NOSIGNAL
        constexpr int send_flags = MSG_NOSIGNAL;
#else
        constexpr int send_flags = 0;
#endif

        void prepare_socket(int fd, bool nonblocking)
        {
            ::fcntl(fd, F_SETFD, FD_CLOEXEC);
            if (nonblocking) ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
#ifdef SO_NOSIGPIPE
            int enabled = 1;
            ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled));
#endif
        }

        std::string system_error(const char *operation)
        {
            return std::string(operation) + ": " + std::strerror(errno);
        }

        // Unsent bytes for one direction. Compacts lazily so partial writes
        // stay O(1) without letting the consumed prefix grow unbounded.
        class PendingBytes
        {
          public:
            bool empty() const { return offset_ == bytes_.size(); }
            std::size_t size() const { return bytes_.size() - offset_; }
            bool has_room() const { return size() < max_pending_bytes; }
            const char *data() const { return bytes_.data() + offset_; }

            void append(const char *data, std::size_t count)
            {
                if (offset_ > relay_chunk_bytes && offset_ * 2 > bytes_.size())
                {
                    bytes_.erase(0, offset_);
                    offset_ = 0;
                }
                bytes_.append(data, count);
            }

            void consume(std::size_t count)
            {
                offset_ += count;
                if (offset_ != bytes_.size()) return;
                bytes_.clear();
                offset_ = 0;
            }

          private:
            std::string bytes_;
            std::size_t offset_{0};
        };

        using CurlHandle = std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>;
    } // namespace

    WebSocketTunnel::WebSocketTunnel(const std::string &remote_url, NetworkRoute route)
        : route_(std::move(route))
    {
        std::string protocol;
        std::string host;
        std::string path;
        std::string query;
        int port = 0;
        if (!ix::UrlParser::parse(remote_url, protocol, host, path, query, port) ||
            (protocol != "ws" && protocol != "wss") || host.empty())
        {
            throw std::invalid_argument("WebSocket URL must use ws:// or wss://: " + remote_url);
        }

        const auto authority = (host.find(':') != std::string::npos ? "[" + host + "]" : host) +
                               ":" + std::to_string(port);
        upstream_url_ = (protocol == "wss" ? "https://" : "http://") + authority + "/";
        host_header_ = authority;
        origin_header_ = protocol + "://" + authority;

        acquire_http_global();
        global_acquired_ = true;
        try
        {
            int wake[2] = {-1, -1};
            if (::pipe(wake) != 0) throw std::runtime_error(system_error("pipe"));
            wake_read_fd_ = wake[0];
            wake_write_fd_ = wake[1];
            prepare_socket(wake_read_fd_, true);
            prepare_socket(wake_write_fd_, true);

            listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
            if (listen_fd_ < 0) throw std::runtime_error(system_error("socket"));
            prepare_socket(listen_fd_, true);
            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            address.sin_port = 0;
            socklen_t length = sizeof(address);
            if (::bind(listen_fd_, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0 ||
                ::listen(listen_fd_, 4) != 0 ||
                ::getsockname(listen_fd_, reinterpret_cast<sockaddr *>(&address), &length) != 0)
            {
                throw std::runtime_error(system_error("WebSocket tunnel listener"));
            }
            local_url_ = "ws://127.0.0.1:" + std::to_string(ntohs(address.sin_port)) + path;
            worker_ = std::thread([this] { run(); });
        }
        catch (...)
        {
            for (const int fd : {listen_fd_, wake_read_fd_, wake_write_fd_})
                if (fd >= 0) ::close(fd);
            release_http_global();
            throw;
        }
    }

    WebSocketTunnel::~WebSocketTunnel()
    {
        stopping_.store(true);
        const char wake = 1;
        (void)::write(wake_write_fd_, &wake, 1);
        if (worker_.joinable()) worker_.join();
        for (const int fd : {listen_fd_, wake_read_fd_, wake_write_fd_})
            ::close(fd);
        if (global_acquired_) release_http_global();
    }

    std::string WebSocketTunnel::last_error() const
    {
        std::lock_guard<std::mutex> lock(error_mutex_);
        return last_error_;
    }

    void WebSocketTunnel::set_error(std::string message)
    {
        std::lock_guard<std::mutex> lock(error_mutex_);
        last_error_ = std::move(message);
    }

    void WebSocketTunnel::run()
    {
        while (!stopping_.load())
        {
            pollfd fds[2] = {{listen_fd_, POLLIN, 0}, {wake_read_fd_, POLLIN, 0}};
            if (::poll(fds, 2, -1) < 0)
            {
                if (errno == EINTR) continue;
                set_error(system_error("poll"));
                return;
            }
            if (fds[1].revents != 0) return;
            const int client_fd = ::accept(listen_fd_, nullptr, nullptr);
            if (client_fd < 0) continue;
            prepare_socket(client_fd, true);
            int enabled = 1;
            ::setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof(enabled));
            serve(client_fd);
            ::close(client_fd);
        }
    }

    void WebSocketTunnel::serve(int client_fd)
    {
        CurlHandle upstream(open_upstream(), curl_easy_cleanup);
        if (!upstream) return;
        curl_socket_t upstream_fd = CURL_SOCKET_BAD;
        if (curl_easy_getinfo(upstream.get(), CURLINFO_ACTIVESOCKET, &upstream_fd) != CURLE_OK ||
            upstream_fd == CURL_SOCKET_BAD)
        {
            set_error("WebSocket tunnel could not read the upstream socket");
            return;
        }
        set_error("");
        relay(client_fd, upstream.get(), upstream_fd);
    }

    int WebSocketTunnel::abort_on_stop(void *tunnel, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
    {
        return static_cast<WebSocketTunnel *>(tunnel)->stopping_.load() ? 1 : 0;
    }

    CURL *WebSocketTunnel::open_upstream()
    {
        CurlHandle curl(curl_easy_init(), curl_easy_cleanup);
        if (!curl)
        {
            set_error("WebSocket tunnel could not create a libcurl handle");
            return nullptr;
        }
        char error_buffer[CURL_ERROR_SIZE] = {};
        CURL *handle = curl.get();
        curl_easy_setopt(handle, CURLOPT_URL, upstream_url_.c_str());
        curl_easy_setopt(handle, CURLOPT_CONNECT_ONLY, 1L);
        // ALPN must offer only HTTP/1.1; the WebSocket upgrade is an HTTP/1.1 request.
        curl_easy_setopt(handle, CURLOPT_HTTP_VERSION, static_cast<long>(CURL_HTTP_VERSION_1_1));
        curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT_MS, upstream_connect_timeout_ms);
        curl_easy_setopt(handle, CURLOPT_TCP_NODELAY, 1L);
        curl_easy_setopt(handle, CURLOPT_TCP_KEEPALIVE, 1L);
        curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 2L);
        curl_easy_setopt(handle, CURLOPT_ERRORBUFFER, error_buffer);
        curl_easy_setopt(handle, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(handle, CURLOPT_XFERINFOFUNCTION, abort_on_stop);
        curl_easy_setopt(handle, CURLOPT_XFERINFODATA, this);
        apply_network_route(handle, route_);
        // Also tunnel ws:// through proxies taken from the environment.
        curl_easy_setopt(handle, CURLOPT_HTTPPROXYTUNNEL, 1L);

        const auto code = curl_easy_perform(handle);
        curl_easy_setopt(handle, CURLOPT_ERRORBUFFER, nullptr);
        if (code != CURLE_OK)
        {
            set_error(std::string("WebSocket tunnel could not reach ") + host_header_ + ": " +
                      (error_buffer[0] != '\0' ? error_buffer : curl_easy_strerror(code)));
            return nullptr;
        }
        return curl.release();
    }

    void WebSocketTunnel::relay(int client_fd, CURL *upstream, curl_socket_t upstream_fd)
    {
        PendingBytes to_upstream;
        PendingBytes to_client;
        bool client_eof = false;
        bool upstream_eof = false;
        char chunk[relay_chunk_bytes];

        while (!stopping_.load())
        {
            // Read both sides before polling: libcurl can hold decrypted bytes
            // that no longer make the socket readable.
            while (!client_eof && to_upstream.has_room())
            {
                const auto received = ::recv(client_fd, chunk, sizeof(chunk), 0);
                if (received > 0)
                    to_upstream.append(chunk, static_cast<std::size_t>(received));
                else if (received == 0)
                    client_eof = true;
                else if (errno == EINTR)
                    continue;
                else if (errno == EAGAIN || errno == EWOULDBLOCK)
                    break;
                else
                    return;
            }
            while (!upstream_eof && to_client.has_room())
            {
                std::size_t received = 0;
                const auto code = curl_easy_recv(upstream, chunk, sizeof(chunk), &received);
                if (code == CURLE_AGAIN) break;
                if (code != CURLE_OK)
                {
                    set_error(std::string("WebSocket tunnel read failed: ") +
                              curl_easy_strerror(code));
                    upstream_eof = true;
                }
                else if (received == 0)
                    upstream_eof = true;
                else
                    to_client.append(chunk, received);
            }

            while (!client_eof && !to_client.empty())
            {
                const auto sent = ::send(client_fd, to_client.data(), to_client.size(), send_flags);
                if (sent > 0)
                    to_client.consume(static_cast<std::size_t>(sent));
                else if (sent < 0 && errno == EINTR)
                    continue;
                else if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
                    break;
                else
                    return;
            }
            while (!upstream_eof && !to_upstream.empty())
            {
                std::size_t sent = 0;
                const auto code =
                    curl_easy_send(upstream, to_upstream.data(), to_upstream.size(), &sent);
                if (code == CURLE_AGAIN) break;
                if (code != CURLE_OK)
                {
                    set_error(std::string("WebSocket tunnel write failed: ") +
                              curl_easy_strerror(code));
                    return;
                }
                to_upstream.consume(sent);
            }

            // Close both legs once either side has closed and its last bytes
            // were delivered to the other side.
            if ((upstream_eof && to_client.empty()) || (client_eof && to_upstream.empty()) ||
                (upstream_eof && client_eof))
                return;

            const short client_events =
                static_cast<short>((!client_eof && to_upstream.has_room() ? POLLIN : 0) |
                                   (!client_eof && !to_client.empty() ? POLLOUT : 0));
            const short upstream_events =
                static_cast<short>((!upstream_eof && to_client.has_room() ? POLLIN : 0) |
                                   (!upstream_eof && !to_upstream.empty() ? POLLOUT : 0));
            pollfd fds[3] = {
                {client_events != 0 ? client_fd : -1, client_events, 0},
                {upstream_events != 0 ? static_cast<int>(upstream_fd) : -1, upstream_events, 0},
                {wake_read_fd_, POLLIN, 0}};
            if (::poll(fds, 3, -1) < 0 && errno != EINTR)
            {
                set_error(system_error("poll"));
                return;
            }
            if (fds[2].revents != 0) return;
        }
    }
} // namespace polymarket::detail
