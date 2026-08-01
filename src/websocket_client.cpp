#include "websocket_client.hpp"
#include "websocket_client_state.hpp"

namespace polymarket
{
    WebSocketClient::WebSocketClient()
        : state_(detail::WebSocketClientState::create())
    {
    }

    WebSocketClient::~WebSocketClient()
    {
        auto state = state_;
        state->stop();
    }

    void WebSocketClient::set_url(const std::string &url)
    {
        auto state = state_;
        state->set_url(url);
    }

    void WebSocketClient::set_ping_interval_ms(int interval_ms)
    {
        auto state = state_;
        state->set_ping_interval_ms(interval_ms);
    }

    void WebSocketClient::set_auto_reconnect(bool enabled)
    {
        auto state = state_;
        state->set_auto_reconnect(enabled);
    }

    void WebSocketClient::configure(const WebSocketOptions &options)
    {
        auto state = state_;
        state->configure(options);
    }

    WebSocketOptions WebSocketClient::options() const
    {
        auto state = state_;
        return state->options();
    }

    void WebSocketClient::on_message(OnMessageCallback callback)
    {
        auto state = state_;
        state->on_message(std::move(callback));
    }

    void WebSocketClient::on_sequenced_message(OnSequencedMessageCallback callback)
    {
        auto state = state_;
        state->on_sequenced_message(std::move(callback));
    }

    void WebSocketClient::on_typed_message(OnTypedMessageCallback callback)
    {
        auto state = state_;
        state->on_typed_message(std::move(callback));
    }

    void WebSocketClient::on_connect(OnConnectCallback callback)
    {
        auto state = state_;
        state->on_connect(std::move(callback));
    }

    void WebSocketClient::on_disconnect(OnDisconnectCallback callback)
    {
        auto state = state_;
        state->on_disconnect(std::move(callback));
    }

    void WebSocketClient::on_error(OnErrorCallback callback)
    {
        auto state = state_;
        state->on_error(std::move(callback));
    }

    void WebSocketClient::on_stream_gap(OnStreamGapCallback callback)
    {
        auto state = state_;
        state->on_stream_gap(std::move(callback));
    }

    bool WebSocketClient::connect()
    {
        auto state = state_;
        return state->connect();
    }

    bool WebSocketClient::wait_until_connected(std::chrono::milliseconds timeout)
    {
        auto state = state_;
        return state->wait_until_connected(timeout);
    }

    void WebSocketClient::disconnect()
    {
        auto state = state_;
        state->disconnect();
    }

    bool WebSocketClient::is_connected() const
    {
        auto state = state_;
        return state->is_connected();
    }

    WsState WebSocketClient::state() const
    {
        auto state = state_;
        return state->state();
    }

    bool WebSocketClient::send(const std::string &message)
    {
        auto state = state_;
        return state->send(message);
    }

    void WebSocketClient::track_subscription(const std::string &message)
    {
        auto state = state_;
        state->track_subscription(message);
    }

    void WebSocketClient::untrack_subscription(const std::string &message)
    {
        auto state = state_;
        state->untrack_subscription(message);
    }

    void WebSocketClient::clear_subscriptions()
    {
        auto state = state_;
        state->clear_subscriptions();
    }

    void WebSocketClient::replace_subscriptions(std::vector<std::string> messages)
    {
        auto state = state_;
        state->replace_subscriptions(std::move(messages));
    }

    std::size_t WebSocketClient::tracked_subscription_count() const
    {
        auto state = state_;
        return state->tracked_subscription_count();
    }

    void WebSocketClient::request_resnapshot(const std::string &reason)
    {
        auto state = state_;
        state->request_resnapshot(reason);
    }

    void WebSocketClient::run()
    {
        auto state = state_;
        state->run();
    }

    void WebSocketClient::stop()
    {
        auto state = state_;
        state->stop();
    }

    uint64_t WebSocketClient::messages_received() const
    {
        auto state = state_;
        return state->messages_received();
    }

    uint64_t WebSocketClient::bytes_received() const
    {
        auto state = state_;
        return state->bytes_received();
    }

    uint64_t WebSocketClient::stream_generation() const
    {
        auto state = state_;
        return state->stream_generation();
    }

    WebSocketStats WebSocketClient::stats() const
    {
        auto state = state_;
        return state->stats();
    }
}
