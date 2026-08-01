#include "websocket_client_state.hpp"
#include "websocket_resilience.hpp"

namespace polymarket::detail
{
    void WebSocketClientState::start_transport_worker()
    {
        std::lock_guard<std::mutex> lock(transport_mutex_);
        if (transport_worker_active_) return;

        transport_worker_active_ = true;
        transport_stop_requested_ = false;
        transport_stop_pending_.store(false);
        auto self = shared_from_this();
        try
        {
            std::thread worker([self] { self->transport_worker_loop(); });
            worker.detach();
        }
        catch (...)
        {
            transport_worker_active_ = false;
            transport_cv_.notify_all();
            throw;
        }
    }

    bool WebSocketClientState::request_transport_stop()
    {
        std::lock_guard<std::mutex> lock(transport_mutex_);
        if (!transport_worker_active_) return false;
        transport_stop_pending_.store(true);
        transport_stop_requested_ = true;
        transport_cv_.notify_all();
        return true;
    }

    void WebSocketClientState::wait_for_transport_stop()
    {
        std::unique_lock<std::mutex> lock(transport_mutex_);
        transport_cv_.wait(lock, [this] { return !transport_worker_active_; });
    }

    void WebSocketClientState::transport_worker_loop()
    {
        {
            std::unique_lock<std::mutex> lock(transport_mutex_);
            transport_cv_.wait(lock, [this] { return transport_stop_requested_; });
        }

        finish_disconnect();
        publish_disconnected();
        {
            std::lock_guard<std::mutex> lock(transport_mutex_);
            transport_worker_active_ = false;
            transport_stop_requested_ = false;
            transport_stop_pending_.store(false);
        }
        transport_cv_.notify_all();
    }

    void WebSocketClientState::finish_disconnect()
    {
        ws_.stop();
        stop_message_worker();
        ws_.setOnMessageCallback(nullptr);
    }

    void WebSocketClientState::publish_disconnected()
    {
        state_.store(WsState::DISCONNECTED);
        state_cv_.notify_all();
    }
}
