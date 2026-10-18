#include "public_stream.hpp"

#include <stdexcept>
#include <utility>

namespace polymarket::detail
{
    std::shared_ptr<PublicStream> PublicStream::create(const std::string &url,
                                                       const WebSocketOptions &options,
                                                       std::chrono::milliseconds connect_timeout,
                                                       MessageHandler handler)
    {
        auto stream = std::shared_ptr<PublicStream>(
            new PublicStream(url, options, connect_timeout, std::move(handler)));
        stream->bind_websocket_callbacks();
        return stream;
    }

    PublicStream::PublicStream(const std::string &url, const WebSocketOptions &options,
                               std::chrono::milliseconds connect_timeout, MessageHandler handler)
        : connect_timeout_(connect_timeout), handler_(std::move(handler))
    {
        if (url.empty()) throw std::invalid_argument("stream URL must not be empty");
        if (connect_timeout.count() <= 0)
            throw std::invalid_argument("ws_connect_timeout_ms must be positive");
        websocket_.configure(options);
        websocket_.set_url(url);
    }

    PublicStream::~PublicStream()
    {
        shutdown();
    }

    void PublicStream::bind_websocket_callbacks()
    {
        std::weak_ptr<PublicStream> weak = shared_from_this();
        websocket_.on_sequenced_message(
            [weak](const std::string &message, std::uint64_t generation)
            {
                if (auto stream = weak.lock()) stream->handle_message(message, generation);
            });
        websocket_.on_stream_gap(
            [weak](std::uint64_t generation)
            {
                if (auto stream = weak.lock()) stream->handle_gap(generation);
            });
        websocket_.on_connect(
            [weak]
            {
                if (auto stream = weak.lock()) stream->handle_connect();
            });
        websocket_.on_error(
            [weak](const std::string &error)
            {
                if (auto stream = weak.lock()) stream->handle_error(error);
            });
    }

    void PublicStream::shutdown()
    {
        if (shutdown_started_.exchange(true)) return;
        owner_active_.store(false, std::memory_order_release);
        stream_active_.store(false, std::memory_order_release);
        {
            std::lock_guard lock(callback_update_mutex_);
            std::shared_ptr<const PublicStreamCallbacks> empty =
                std::make_shared<PublicStreamCallbacks>();
            std::atomic_store_explicit(&callbacks_, std::move(empty), std::memory_order_release);
        }
        websocket_.stop();
    }

    void PublicStream::on_stream_gap(std::function<void()> callback)
    {
        update_callbacks([callback = std::move(callback)](auto &callbacks) mutable
                         { callbacks.gap = std::move(callback); });
    }

    void PublicStream::on_stream_recovered(std::function<void()> callback)
    {
        update_callbacks([callback = std::move(callback)](auto &callbacks) mutable
                         { callbacks.recovered = std::move(callback); });
    }

    void PublicStream::on_error(std::function<void(const std::string &)> callback)
    {
        update_callbacks([callback = std::move(callback)](auto &callbacks) mutable
                         { callbacks.error = std::move(callback); });
    }

    bool PublicStream::active() const
    {
        return owner_active_.load(std::memory_order_acquire) &&
               stream_active_.load(std::memory_order_acquire);
    }

    void PublicStream::handle_message(const std::string &message, std::uint64_t generation)
    {
        if (!active() || generation != websocket_.stream_generation()) return;
        handler_(message);
    }

    void PublicStream::handle_gap(std::uint64_t generation)
    {
        if (!active()) return;
        recovery_generation_.store(generation, std::memory_order_release);
        const auto callbacks = callbacks_snapshot();
        if (callbacks->gap) callbacks->gap();
    }

    void PublicStream::handle_connect()
    {
        if (!active()) return;
        const auto pending = recovery_generation_.exchange(0, std::memory_order_acq_rel);
        if (pending == 0 || pending != websocket_.stream_generation()) return;
        const auto callbacks = callbacks_snapshot();
        if (callbacks->recovered) callbacks->recovered();
    }

    void PublicStream::handle_error(const std::string &error)
    {
        if (!active()) return;
        const auto callbacks = callbacks_snapshot();
        if (callbacks->error) callbacks->error(error);
    }

    bool PublicStream::connect()
    {
        if (!owner_active_.load(std::memory_order_acquire)) return false;
        stream_active_.store(true, std::memory_order_release);
        const bool connected =
            websocket_.connect() && websocket_.wait_until_connected(connect_timeout_);
        if (!connected) disconnect();
        return connected;
    }

    void PublicStream::disconnect()
    {
        stream_active_.store(false, std::memory_order_release);
        websocket_.disconnect();
    }

    bool PublicStream::is_connected() const
    {
        return owner_active_.load(std::memory_order_acquire) && websocket_.is_connected();
    }

    void PublicStream::run()
    {
        if (owner_active_.load(std::memory_order_acquire)) websocket_.run();
    }

    void PublicStream::stop()
    {
        stream_active_.store(false, std::memory_order_release);
        websocket_.stop();
    }

    void PublicStream::replace_subscriptions(std::vector<std::string> messages)
    {
        websocket_.replace_subscriptions(std::move(messages));
    }

    bool PublicStream::send(const std::string &message)
    {
        return websocket_.send(message);
    }

    void PublicStream::request_resnapshot(const std::string &reason)
    {
        websocket_.request_resnapshot(reason);
    }
} // namespace polymarket::detail
