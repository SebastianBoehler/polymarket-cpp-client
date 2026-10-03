#include "user_stream_protocol.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>

using json = nlohmann::json;

namespace polymarket::detail
{
    namespace
    {
        std::vector<std::string> difference(const std::vector<std::string> &left,
                                            const std::vector<std::string> &right)
        {
            std::vector<std::string> result;
            for (const auto &value : left)
            {
                if (std::find(right.begin(), right.end(), value) == right.end())
                    result.push_back(value);
            }
            return result;
        }
    }

    std::string user_subscription_message(const UserSubscriptionState &state,
                                          const ApiCredentials &credentials)
    {
        json message{{"auth", {{"apiKey", credentials.api_key},
                               {"secret", credentials.api_secret},
                               {"passphrase", credentials.api_passphrase}}},
                     {"type", "user"}};
        if (!state.all_markets)
        {
            message["markets"] = state.markets;
        }
        return message.dump();
    }

    std::string user_subscription_update_message(const std::vector<std::string> &markets,
                                                 bool subscribe)
    {
        return json{{"markets", markets},
                    {"operation", subscribe ? "subscribe" : "unsubscribe"}}
            .dump();
    }

    UserSubscriptionSync plan_user_subscription_sync(const UserSubscriptionState &before,
                                                     const UserSubscriptionState &after,
                                                     const ApiCredentials &credentials)
    {
        UserSubscriptionSync sync;
        if (!after.active())
        {
            // An empty market list means "all markets" to the server, so the
            // last unsubscribe must drop the authenticated session instead.
            sync.reconnect = before.active();
            return sync;
        }
        if (!before.active())
        {
            sync.messages.push_back(user_subscription_message(after, credentials));
            return sync;
        }
        if (after.all_markets)
        {
            if (!before.all_markets)
                sync.messages.push_back(
                    user_subscription_update_message(before.markets, false));
            return sync;
        }
        if (before.all_markets)
        {
            sync.messages.push_back(
                user_subscription_update_message(after.markets, true));
            return sync;
        }

        const auto added = difference(after.markets, before.markets);
        const auto removed = difference(before.markets, after.markets);
        if (!added.empty())
            sync.messages.push_back(user_subscription_update_message(added, true));
        if (!removed.empty())
            sync.messages.push_back(user_subscription_update_message(removed, false));
        return sync;
    }
}
