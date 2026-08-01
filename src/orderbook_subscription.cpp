#include "orderbook_subscription.hpp"
#include "websocket_client.hpp"

namespace polymarket::detail
{
    void recover_failed_subscription_send(WebSocketClient &websocket)
    {
        websocket.request_resnapshot(
            "subscription update send failed; resnapshot required");
    }
}
