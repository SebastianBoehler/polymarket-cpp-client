#include "user_stream_runtime.hpp"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace polymarket::detail
{
    namespace
    {
        // CLOB closes the user channel with policy violation on bad credentials.
        constexpr uint16_t policy_violation_close_code = 1008;

        Config validated_config(Config config)
        {
            if (config.clob_user_ws_url.empty())
            {
                throw std::invalid_argument("clob_user_ws_url must not be empty");
            }
            if (config.ws_ping_interval_ms <= 0)
            {
                throw std::invalid_argument(
                    "ws_ping_interval_ms must be positive");
            }
            if (config.ws_connect_timeout_ms <= 0)
            {
                throw std::invalid_argument(
                    "ws_connect_timeout_ms must be positive");
            }
            return config;
        }

        ApiCredentials validated_credentials(ApiCredentials credentials)
        {
            if (credentials.api_key.empty() || credentials.api_secret.empty() ||
                credentials.api_passphrase.empty())
            {
                throw std::invalid_argument(
                    "user stream requires API key, secret, and passphrase");
            }
            return credentials;
        }

        void add_markets(std::vector<std::string> &markets,
                         const std::vector<std::string> &condition_ids)
        {
            for (const auto &condition_id : condition_ids)
            {
                if (condition_id.empty() ||
                    std::find(markets.begin(), markets.end(), condition_id) !=
                        markets.end())
                    continue;
                markets.push_back(condition_id);
            }
        }
    }

    std::shared_ptr<UserStreamRuntime> UserStreamRuntime::create(
        const Config &config, const ApiCredentials &credentials)
    {
        auto runtime = std::shared_ptr<UserStreamRuntime>(
            new UserStreamRuntime(config, credentials));
        runtime->bind_websocket_callbacks();
        return runtime;
    }

    UserStreamRuntime::UserStreamRuntime(const Config &config,
                                         const ApiCredentials &credentials)
        : config_(validated_config(config)),
          credentials_(validated_credentials(credentials))
    {
        websocket_.set_url(config_.clob_user_ws_url);
        websocket_.set_ping_interval_ms(config_.ws_ping_interval_ms);
        websocket_.set_auto_reconnect(true);
    }

    UserStreamRuntime::~UserStreamRuntime()
    {
        shutdown();
    }

    void UserStreamRuntime::bind_websocket_callbacks()
    {
        std::weak_ptr<UserStreamRuntime> weak = shared_from_this();
        websocket_.on_sequenced_message(
            [weak](const std::string &message, uint64_t generation)
            {
                if (auto runtime = weak.lock())
                    runtime->handle_message(message, generation);
            });
        websocket_.on_stream_gap(
            [weak](uint64_t)
            {
                if (auto runtime = weak.lock())
                    runtime->handle_stream_gap();
            });
        websocket_.on_close(
            [weak](uint16_t code, const std::string &reason)
            {
                if (auto runtime = weak.lock())
                    runtime->handle_close(code, reason);
            });
        websocket_.on_connect([]
                              { std::cout << "[WS] Connected to CLOB user stream\n"; });
        websocket_.on_disconnect([]
                                 { std::cout << "[WS] Disconnected from CLOB user stream\n"; });
        websocket_.on_error([](const std::string &error)
                            { std::cerr << "[WS] Error: " << error << '\n'; });
    }

    void UserStreamRuntime::shutdown()
    {
        if (shutdown_started_.exchange(true)) return;
        owner_active_.store(false, std::memory_order_release);
        deactivate_stream();
        clear_callbacks();
        websocket_.stop();
    }

    void UserStreamRuntime::subscribe_all_markets()
    {
        update_subscription([](UserSubscriptionState &state)
                            { state.all_markets = true; });
    }

    void UserStreamRuntime::subscribe(const std::vector<std::string> &condition_ids)
    {
        update_subscription([&condition_ids](UserSubscriptionState &state)
                            { add_markets(state.markets, condition_ids); });
    }

    void UserStreamRuntime::unsubscribe(const std::vector<std::string> &condition_ids)
    {
        update_subscription(
            [&condition_ids](UserSubscriptionState &state)
            {
                auto &markets = state.markets;
                markets.erase(std::remove_if(markets.begin(), markets.end(),
                                             [&condition_ids](const auto &market)
                                             {
                                                 return std::find(condition_ids.begin(),
                                                                  condition_ids.end(),
                                                                  market) != condition_ids.end();
                                             }),
                              markets.end());
            });
    }

    void UserStreamRuntime::unsubscribe_all()
    {
        update_subscription([](UserSubscriptionState &state)
                            { state = {}; });
    }

    bool UserStreamRuntime::is_subscribed_to_all_markets() const
    {
        std::lock_guard lock(subscriptions_mutex_);
        return subscription_.all_markets;
    }

    std::vector<std::string> UserStreamRuntime::subscribed_markets() const
    {
        std::lock_guard lock(subscriptions_mutex_);
        return subscription_.markets;
    }

    std::vector<std::string> UserStreamRuntime::tracked_messages() const
    {
        return subscription_.active()
                   ? std::vector<std::string>{user_subscription_message(
                         subscription_, credentials_)}
                   : std::vector<std::string>{};
    }

    void UserStreamRuntime::on_order(UserOrderCallback callback)
    {
        update_callbacks([callback = std::move(callback)](auto &callbacks) mutable
                         { callbacks.order = std::move(callback); });
    }

    void UserStreamRuntime::on_trade(UserTradeCallback callback)
    {
        update_callbacks([callback = std::move(callback)](auto &callbacks) mutable
                         { callbacks.trade = std::move(callback); });
    }

    void UserStreamRuntime::on_stream_gap(UserStreamGapCallback callback)
    {
        update_callbacks([callback = std::move(callback)](auto &callbacks) mutable
                         { callbacks.gap = std::move(callback); });
    }

    void UserStreamRuntime::on_error(UserStreamErrorCallback callback)
    {
        update_callbacks([callback = std::move(callback)](auto &callbacks) mutable
                         { callbacks.error = std::move(callback); });
    }

    void UserStreamRuntime::clear_callbacks()
    {
        std::lock_guard lock(callback_update_mutex_);
        std::shared_ptr<const UserStreamCallbacks> empty =
            std::make_shared<UserStreamCallbacks>();
        std::atomic_store_explicit(&callbacks_, std::move(empty),
                                   std::memory_order_release);
    }

    bool UserStreamRuntime::stream_is_current(uint64_t user_generation,
                                              uint64_t websocket_generation) const
    {
        return owner_active_.load(std::memory_order_acquire) &&
               stream_active_.load(std::memory_order_acquire) &&
               user_generation == stream_generation_.load() &&
               websocket_generation == websocket_.stream_generation();
    }

    bool UserStreamRuntime::connect()
    {
        if (!owner_active_.load(std::memory_order_acquire)) return false;
        authentication_failed_.store(false, std::memory_order_release);
        stream_active_.store(true, std::memory_order_release);
        const bool connected = websocket_.connect() &&
                               websocket_.wait_until_connected(
                                   std::chrono::milliseconds(
                                       config_.ws_connect_timeout_ms));
        if (!connected) deactivate_stream();
        return connected;
    }

    void UserStreamRuntime::disconnect()
    {
        deactivate_stream();
        websocket_.disconnect();
    }

    bool UserStreamRuntime::is_connected() const
    {
        return owner_active_.load(std::memory_order_acquire) &&
               websocket_.is_connected();
    }

    bool UserStreamRuntime::authentication_failed() const
    {
        return authentication_failed_.load(std::memory_order_acquire);
    }

    void UserStreamRuntime::run()
    {
        if (owner_active_.load(std::memory_order_acquire)) websocket_.run();
    }

    void UserStreamRuntime::stop()
    {
        deactivate_stream();
        websocket_.stop();
    }

    void UserStreamRuntime::deactivate_stream()
    {
        if (stream_active_.exchange(false, std::memory_order_acq_rel))
            stream_generation_.fetch_add(1);
    }

    void UserStreamRuntime::handle_stream_gap()
    {
        if (!owner_active_.load(std::memory_order_acquire) ||
            !stream_active_.load(std::memory_order_acquire))
            return;
        stream_generation_.fetch_add(1);
        const auto callbacks = callbacks_snapshot();
        if (callbacks->gap) callbacks->gap();
    }

    void UserStreamRuntime::handle_close(uint16_t code, const std::string &reason)
    {
        if (code != policy_violation_close_code ||
            !owner_active_.load(std::memory_order_acquire) ||
            !stream_active_.load(std::memory_order_acquire))
            return;
        // Reconnecting with rejected credentials would loop forever.
        authentication_failed_.store(true, std::memory_order_release);
        deactivate_stream();
        websocket_.disconnect();
        const auto error = "user stream rejected by server (" + std::to_string(code) +
                           "): " + reason;
        std::cerr << "[WS] " << error << '\n';
        const auto callbacks = callbacks_snapshot();
        if (callbacks->error) callbacks->error(error);
    }

    void UserStreamRuntime::handle_message(const std::string &message,
                                           uint64_t websocket_generation)
    {
        const auto generation = stream_generation_.load();
        if (!stream_is_current(generation, websocket_generation) ||
            message.empty() || message == "PONG")
            return;

        std::vector<UserStreamEvent> events;
        try
        {
            events = parse_user_events(message);
        }
        catch (const std::exception &error)
        {
            if (!owner_active_.load(std::memory_order_acquire)) return;
            std::cerr << "[WS] User stream parse error: " << error.what() << '\n';
            websocket_.request_resnapshot(
                "invalid user data; reconnect required");
            return;
        }

        // Callback exceptions propagate to WebSocketClient, which counts them
        // and requests a reconnect.
        for (const auto &event : events)
        {
            if (!stream_is_current(generation, websocket_generation)) return;
            const auto callbacks = callbacks_snapshot();
            std::visit(
                [this, &callbacks](const auto &payload)
                {
                    using Event = std::decay_t<decltype(payload)>;
                    if constexpr (std::is_same_v<Event, UserOrderEvent>)
                    {
                        order_events_++;
                        if (callbacks->order) callbacks->order(payload);
                    }
                    else
                    {
                        trade_events_++;
                        if (callbacks->trade) callbacks->trade(payload);
                    }
                },
                event);
        }
    }

    uint64_t UserStreamRuntime::order_events() const
    {
        return order_events_.load();
    }

    uint64_t UserStreamRuntime::trade_events() const
    {
        return trade_events_.load();
    }
}
