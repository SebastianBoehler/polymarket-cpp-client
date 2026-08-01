#pragma once

namespace polymarket
{
    class WebSocketClient;

    namespace detail
    {
        void recover_failed_subscription_send(WebSocketClient &websocket);
    }
}
