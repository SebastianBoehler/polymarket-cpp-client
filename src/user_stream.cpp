#include "user_stream.hpp"
#include "user_stream_runtime.hpp"

namespace polymarket
{
    UserStream::UserStream(const Config &config, const ApiCredentials &credentials)
        : runtime_(detail::UserStreamRuntime::create(config, credentials))
    {
    }

    UserStream::~UserStream()
    {
        auto runtime = std::move(runtime_);
        if (runtime) runtime->shutdown();
    }

    void UserStream::subscribe_all_markets()
    {
        auto runtime = runtime_;
        if (runtime) runtime->subscribe_all_markets();
    }

    void UserStream::subscribe(const std::vector<std::string> &condition_ids)
    {
        auto runtime = runtime_;
        if (runtime) runtime->subscribe(condition_ids);
    }

    void UserStream::subscribe(const std::string &condition_id)
    {
        subscribe(std::vector<std::string>{condition_id});
    }

    void UserStream::unsubscribe(const std::vector<std::string> &condition_ids)
    {
        auto runtime = runtime_;
        if (runtime) runtime->unsubscribe(condition_ids);
    }

    void UserStream::unsubscribe(const std::string &condition_id)
    {
        unsubscribe(std::vector<std::string>{condition_id});
    }

    void UserStream::unsubscribe_all()
    {
        auto runtime = runtime_;
        if (runtime) runtime->unsubscribe_all();
    }

    bool UserStream::is_subscribed_to_all_markets() const
    {
        auto runtime = runtime_;
        return runtime && runtime->is_subscribed_to_all_markets();
    }

    std::vector<std::string> UserStream::subscribed_markets() const
    {
        auto runtime = runtime_;
        return runtime ? runtime->subscribed_markets() : std::vector<std::string>{};
    }

    void UserStream::on_order(UserOrderCallback callback)
    {
        auto runtime = runtime_;
        if (runtime) runtime->on_order(std::move(callback));
    }

    void UserStream::on_trade(UserTradeCallback callback)
    {
        auto runtime = runtime_;
        if (runtime) runtime->on_trade(std::move(callback));
    }

    void UserStream::on_stream_gap(UserStreamGapCallback callback)
    {
        auto runtime = runtime_;
        if (runtime) runtime->on_stream_gap(std::move(callback));
    }

    void UserStream::on_error(UserStreamErrorCallback callback)
    {
        auto runtime = runtime_;
        if (runtime) runtime->on_error(std::move(callback));
    }

    bool UserStream::connect()
    {
        auto runtime = runtime_;
        return runtime && runtime->connect();
    }

    void UserStream::disconnect()
    {
        auto runtime = runtime_;
        if (runtime) runtime->disconnect();
    }

    bool UserStream::is_connected() const
    {
        auto runtime = runtime_;
        return runtime && runtime->is_connected();
    }

    bool UserStream::authentication_failed() const
    {
        auto runtime = runtime_;
        return runtime && runtime->authentication_failed();
    }

    void UserStream::run()
    {
        auto runtime = runtime_;
        if (runtime) runtime->run();
    }

    void UserStream::stop()
    {
        auto runtime = runtime_;
        if (runtime) runtime->stop();
    }

    uint64_t UserStream::order_events() const
    {
        auto runtime = runtime_;
        return runtime ? runtime->order_events() : 0;
    }

    uint64_t UserStream::trade_events() const
    {
        auto runtime = runtime_;
        return runtime ? runtime->trade_events() : 0;
    }
}
