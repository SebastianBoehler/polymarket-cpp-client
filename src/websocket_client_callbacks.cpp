#include "websocket_callback_context.hpp"
#include "websocket_client_state.hpp"

#include <string>

namespace polymarket::detail
{
    void WebSocketClientState::on_message(OnMessageCallback callback)
    {
        update_callbacks([callback = std::move(callback)](auto &callbacks) mutable
                         { callbacks.message = std::move(callback); });
    }

    void WebSocketClientState::on_sequenced_message(OnSequencedMessageCallback callback)
    {
        update_callbacks([callback = std::move(callback)](auto &callbacks) mutable
                         { callbacks.sequenced_message = std::move(callback); });
    }

    void WebSocketClientState::on_typed_message(OnTypedMessageCallback callback)
    {
        update_callbacks([callback = std::move(callback)](auto &callbacks) mutable
                         { callbacks.typed_message = std::move(callback); });
    }

    void WebSocketClientState::on_connect(OnConnectCallback callback)
    {
        update_callbacks([callback = std::move(callback)](auto &callbacks) mutable
                         { callbacks.connect = std::move(callback); });
    }

    void WebSocketClientState::on_disconnect(OnDisconnectCallback callback)
    {
        update_callbacks([callback = std::move(callback)](auto &callbacks) mutable
                         { callbacks.disconnect = std::move(callback); });
    }

    void WebSocketClientState::on_error(OnErrorCallback callback)
    {
        update_callbacks([callback = std::move(callback)](auto &callbacks) mutable
                         { callbacks.error = std::move(callback); });
    }

    void WebSocketClientState::on_stream_gap(OnStreamGapCallback callback)
    {
        update_callbacks([callback = std::move(callback)](auto &callbacks) mutable
                         { callbacks.stream_gap = std::move(callback); });
    }

    void WebSocketClientState::report_callback_failure(const char *name,
                                                       const char *reason) noexcept
    {
        try
        {
            auto callbacks = callbacks_snapshot();
            const auto &callback = callbacks->error;
            if (!callback) return;
            const std::string message = std::string(name) + " callback failed: " + reason;
            InternalCallbackScope callback_scope(this);
            try
            {
                callback(message);
            }
            catch (...)
            {
                callback_errors_++;
            }
        }
        catch (...)
        {
            callback_errors_++;
        }
    }

    bool WebSocketClientState::invoke_error_callback(const std::string &message) noexcept
    {
        try
        {
            auto callbacks = callbacks_snapshot();
            const auto &callback = callbacks->error;
            if (!callback) return true;
            InternalCallbackScope callback_scope(this);
            callback(message);
            return true;
        }
        catch (...)
        {
            callback_errors_++;
            return false;
        }
    }
}
