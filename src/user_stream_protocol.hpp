#pragma once

#include "user_stream.hpp"

#include <string>
#include <variant>
#include <vector>

namespace polymarket::detail
{
    struct UserSubscriptionState
    {
        bool all_markets{false};
        std::vector<std::string> markets;

        bool active() const { return all_markets || !markets.empty(); }
    };

    // Messages to bring an open socket from one subscription state to another.
    // `reconnect` means the server state cannot be narrowed in place, so the
    // socket must be reopened and the tracked subscription replayed.
    struct UserSubscriptionSync
    {
        std::vector<std::string> messages;
        bool reconnect{false};
    };

    using UserStreamEvent = std::variant<UserOrderEvent, UserTradeEvent>;

    std::string user_subscription_message(const UserSubscriptionState &state,
                                          const ApiCredentials &credentials);
    std::string user_subscription_update_message(const std::vector<std::string> &markets,
                                                 bool subscribe);
    UserSubscriptionSync plan_user_subscription_sync(const UserSubscriptionState &before,
                                                     const UserSubscriptionState &after,
                                                     const ApiCredentials &credentials);
    std::vector<UserStreamEvent> parse_user_events(const std::string &message);
}
