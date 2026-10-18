#include "polymarket/sports_stream.hpp"

#include "public_stream.hpp"
#include "sports_parsing.hpp"

#include <atomic>
#include <exception>
#include <mutex>
#include <utility>

namespace polymarket
{
    namespace detail
    {
        namespace
        {
            constexpr std::uint32_t sports_idle_timeout_ms = 30000;

            WebSocketOptions sports_options()
            {
                WebSocketOptions options;
                options.ping_message.clear();
                options.server_ping_message = "ping";
                options.server_ping_reply = "pong";
                options.idle_timeout_ms = sports_idle_timeout_ms;
                return options;
            }
        } // namespace

        class SportsStreamRuntime final : public std::enable_shared_from_this<SportsStreamRuntime>
        {
          public:
            static std::shared_ptr<SportsStreamRuntime> create(const Config &config)
            {
                auto runtime = std::shared_ptr<SportsStreamRuntime>(new SportsStreamRuntime());
                std::weak_ptr<SportsStreamRuntime> weak = runtime;
                runtime->stream_ =
                    PublicStream::create(config.sports_ws_url, sports_options(),
                                         std::chrono::milliseconds(config.ws_connect_timeout_ms),
                                         [weak](const std::string &message, std::uint64_t)
                                         {
                                             if (auto self = weak.lock())
                                                 self->handle_message(message);
                                         });
                return runtime;
            }

            void shutdown()
            {
                {
                    std::lock_guard lock(callback_mutex_);
                    result_callback_ = std::make_shared<const SportsResultCallback>();
                }
                stream_->shutdown();
            }

            void on_result(SportsResultCallback callback)
            {
                std::lock_guard lock(callback_mutex_);
                result_callback_ =
                    std::make_shared<const SportsResultCallback>(std::move(callback));
            }

            PublicStream &stream() { return *stream_; }
            std::uint64_t result_events() const { return result_events_.load(); }
            std::uint64_t dropped_events() const { return dropped_events_.load(); }

          private:
            SportsStreamRuntime() = default;

            void handle_message(const std::string &message)
            {
                SportsGameResult result;
                try
                {
                    result = parse_sports_message(message);
                }
                catch (const std::exception &)
                {
                    dropped_events_++;
                    return;
                }
                result_events_++;
                std::shared_ptr<const SportsResultCallback> callback;
                {
                    std::lock_guard lock(callback_mutex_);
                    callback = result_callback_;
                }
                if (*callback) (*callback)(result);
            }

            std::shared_ptr<PublicStream> stream_;
            std::mutex callback_mutex_;
            std::shared_ptr<const SportsResultCallback> result_callback_{
                std::make_shared<const SportsResultCallback>()};
            std::atomic<std::uint64_t> result_events_{0};
            std::atomic<std::uint64_t> dropped_events_{0};
        };
    } // namespace detail

    SportsStream::SportsStream(const Config &config)
        : runtime_(detail::SportsStreamRuntime::create(config))
    {
    }

    SportsStream::~SportsStream()
    {
        auto runtime = std::move(runtime_);
        if (runtime) runtime->shutdown();
    }

    void SportsStream::on_result(SportsResultCallback callback)
    {
        runtime_->on_result(std::move(callback));
    }

    void SportsStream::on_stream_gap(SportsStreamGapCallback callback)
    {
        runtime_->stream().on_stream_gap(std::move(callback));
    }

    void SportsStream::on_stream_recovered(SportsStreamRecoveredCallback callback)
    {
        runtime_->stream().on_stream_recovered(std::move(callback));
    }

    void SportsStream::on_error(SportsStreamErrorCallback callback)
    {
        runtime_->stream().on_error(std::move(callback));
    }

    bool SportsStream::connect()
    {
        return runtime_->stream().connect();
    }

    void SportsStream::disconnect()
    {
        runtime_->stream().disconnect();
    }

    bool SportsStream::is_connected() const
    {
        return runtime_->stream().is_connected();
    }

    void SportsStream::run()
    {
        runtime_->stream().run();
    }

    void SportsStream::stop()
    {
        runtime_->stream().stop();
    }

    std::uint64_t SportsStream::result_events() const
    {
        return runtime_->result_events();
    }

    std::uint64_t SportsStream::dropped_events() const
    {
        return runtime_->dropped_events();
    }
} // namespace polymarket
