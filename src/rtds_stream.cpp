#include "polymarket/rtds_stream.hpp"

#include "public_stream.hpp"
#include "rtds_parsing.hpp"

#include <algorithm>
#include <atomic>
#include <exception>
#include <map>
#include <mutex>
#include <utility>

namespace polymarket
{
    namespace detail
    {
        namespace
        {
            constexpr int rtds_ping_interval_ms = 5000;
            constexpr std::uint32_t rtds_idle_timeout_ms = 10 * 60 * 1000;

            WebSocketOptions rtds_options()
            {
                WebSocketOptions options;
                options.ping_message = "PING";
                options.ping_interval_ms = rtds_ping_interval_ms;
                options.idle_timeout_ms = rtds_idle_timeout_ms;
                return options;
            }

            bool contains(const std::vector<RtdsServerKey> &keys, const RtdsServerKey &key)
            {
                return std::find(keys.begin(), keys.end(), key) != keys.end();
            }
        } // namespace

        class RtdsStreamRuntime final : public std::enable_shared_from_this<RtdsStreamRuntime>
        {
          public:
            using Deliver = std::function<void(const RtdsParsedEvent &)>;

            static std::shared_ptr<RtdsStreamRuntime> create(const Config &config)
            {
                auto runtime = std::shared_ptr<RtdsStreamRuntime>(new RtdsStreamRuntime());
                std::weak_ptr<RtdsStreamRuntime> weak = runtime;
                runtime->stream_ =
                    PublicStream::create(config.rtds_ws_url, rtds_options(),
                                         std::chrono::milliseconds(config.ws_connect_timeout_ms),
                                         [weak](const std::string &message)
                                         {
                                             if (auto self = weak.lock())
                                                 self->handle_message(message);
                                         });
                return runtime;
            }

            void shutdown()
            {
                {
                    std::lock_guard lock(mutex_);
                    subscriptions_.clear();
                }
                stream_->shutdown();
            }

            RtdsSubscriptionId add(std::vector<RtdsServerKey> keys, Deliver deliver)
            {
                bool resync = false;
                RtdsSubscriptionId id = 0;
                {
                    std::lock_guard lock(mutex_);
                    const auto before = server_keys();
                    id = ++next_id_;
                    subscriptions_.emplace(id, Subscription{std::move(keys), std::move(deliver)});
                    resync = !sync(before, server_keys());
                }
                if (resync) stream_->request_resnapshot("RTDS subscribe failed; reconnecting");
                return id;
            }

            void remove(RtdsSubscriptionId id)
            {
                bool resync = false;
                {
                    std::lock_guard lock(mutex_);
                    const auto before = server_keys();
                    if (subscriptions_.erase(id) == 0) return;
                    resync = !sync(before, server_keys());
                }
                if (resync) stream_->request_resnapshot("RTDS unsubscribe failed; reconnecting");
            }

            PublicStream &stream() { return *stream_; }
            std::uint64_t events() const { return events_.load(); }
            std::uint64_t dropped_events() const { return dropped_events_.load(); }

          private:
            struct Subscription
            {
                std::vector<RtdsServerKey> keys;
                Deliver deliver;
            };

            RtdsStreamRuntime() = default;

            std::vector<RtdsServerKey> server_keys() const
            {
                std::vector<RtdsServerKey> keys;
                for (const auto &[id, subscription] : subscriptions_)
                {
                    for (const auto &key : subscription.keys)
                    {
                        if (!contains(keys, key)) keys.push_back(key);
                    }
                }
                return keys;
            }

            // False when a frame could not be sent on a live connection.
            bool sync(const std::vector<RtdsServerKey> &before,
                      const std::vector<RtdsServerKey> &after)
            {
                stream_->replace_subscriptions(
                    after.empty()
                        ? std::vector<std::string>{}
                        : std::vector<std::string>{rtds_subscription_frame("subscribe", after)});
                if (!stream_->is_connected()) return true;
                std::vector<RtdsServerKey> added;
                std::vector<RtdsServerKey> removed;
                for (const auto &key : after)
                {
                    if (!contains(before, key)) added.push_back(key);
                }
                for (const auto &key : before)
                {
                    if (!contains(after, key)) removed.push_back(key);
                }
                if (!added.empty() && !stream_->send(rtds_subscription_frame("subscribe", added)))
                    return false;
                return removed.empty() ||
                       stream_->send(rtds_subscription_frame("unsubscribe", removed));
            }

            void handle_message(const std::string &message)
            {
                std::optional<RtdsParsedEvent> event;
                try
                {
                    event = parse_rtds_message(message);
                }
                catch (const std::exception &)
                {
                    dropped_events_++;
                    return;
                }
                if (!event) return;
                events_++;
                std::vector<Deliver> targets;
                {
                    std::lock_guard lock(mutex_);
                    for (const auto &[id, subscription] : subscriptions_)
                        targets.push_back(subscription.deliver);
                }
                for (const auto &deliver : targets)
                    deliver(*event);
            }

            std::shared_ptr<PublicStream> stream_;
            mutable std::mutex mutex_;
            std::map<RtdsSubscriptionId, Subscription> subscriptions_;
            RtdsSubscriptionId next_id_{0};
            std::atomic<std::uint64_t> events_{0};
            std::atomic<std::uint64_t> dropped_events_{0};
        };
    } // namespace detail

    RtdsStream::RtdsStream(const Config &config)
        : runtime_(detail::RtdsStreamRuntime::create(config))
    {
    }

    RtdsStream::~RtdsStream()
    {
        auto runtime = std::move(runtime_);
        if (runtime) runtime->shutdown();
    }

    RtdsSubscriptionId RtdsStream::subscribe_comments(const RtdsCommentFilter &filter,
                                                      RtdsCommentCallback callback)
    {
        auto normalized = detail::normalized_comment_filter(filter);
        auto keys = detail::rtds_comment_keys(normalized);
        return runtime_->add(std::move(keys),
                             [normalized = std::move(normalized),
                              callback = std::move(callback)](const detail::RtdsParsedEvent &event)
                             {
                                 if (callback && event.comment &&
                                     detail::rtds_comment_matches(normalized, *event.comment))
                                     callback(*event.comment);
                             });
    }

    void RtdsStream::unsubscribe(RtdsSubscriptionId id)
    {
        runtime_->remove(id);
    }

    void RtdsStream::on_stream_gap(RtdsStreamGapCallback callback)
    {
        runtime_->stream().on_stream_gap(std::move(callback));
    }

    void RtdsStream::on_stream_recovered(RtdsStreamRecoveredCallback callback)
    {
        runtime_->stream().on_stream_recovered(std::move(callback));
    }

    void RtdsStream::on_error(RtdsStreamErrorCallback callback)
    {
        runtime_->stream().on_error(std::move(callback));
    }

    bool RtdsStream::connect()
    {
        return runtime_->stream().connect();
    }

    void RtdsStream::disconnect()
    {
        runtime_->stream().disconnect();
    }

    bool RtdsStream::is_connected() const
    {
        return runtime_->stream().is_connected();
    }

    void RtdsStream::run()
    {
        runtime_->stream().run();
    }

    void RtdsStream::stop()
    {
        runtime_->stream().stop();
    }

    std::uint64_t RtdsStream::events() const
    {
        return runtime_->events();
    }

    std::uint64_t RtdsStream::dropped_events() const
    {
        return runtime_->dropped_events();
    }
} // namespace polymarket
