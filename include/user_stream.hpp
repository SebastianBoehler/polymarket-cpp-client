#pragma once

#include "clob_types.hpp"
#include "order_signer.hpp"
#include "types.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace polymarket
{
    namespace detail
    {
        class UserStreamRuntime;
    }

    // Order lifecycle event from the authenticated CLOB user channel.
    struct UserOrderEvent
    {
        std::string id;
        std::string owner;
        std::string market;   // condition ID
        std::string asset_id; // token ID
        std::string side;     // BUY or SELL
        std::string original_size;
        std::string size_matched;
        std::string price;
        std::string type; // PLACEMENT, UPDATE, or CANCELLATION
        std::string status;
        std::string order_type;
        std::string maker_address;
        std::string order_owner;
        std::vector<std::string> associate_trades;
        std::string outcome;
        std::string created_at;
        std::string expiration;
        std::string timestamp;
    };

    // Trade lifecycle event (MATCHED, MINED, CONFIRMED, RETRYING, FAILED).
    struct UserTradeEvent
    {
        std::string id;
        std::string taker_order_id;
        std::string market;   // condition ID
        std::string asset_id; // token ID
        std::string side;
        std::string size;
        std::string price;
        std::string status;
        std::string owner;
        std::string fee_rate_bps;
        std::string match_time;
        std::string last_update;
        std::string timestamp;
        std::string trade_owner;
        std::string maker_address;
        std::string transaction_hash;
        std::optional<uint32_t> bucket_index;
        std::vector<MakerOrder> maker_orders;
        std::string trader_side; // TAKER or MAKER
        std::string outcome;
    };

    using UserOrderCallback = std::function<void(const UserOrderEvent &order)>;
    using UserTradeCallback = std::function<void(const UserTradeEvent &trade)>;
    // Fired immediately whenever events may have been missed (every connect,
    // disconnect, queue overflow, or invalid payload). Treat local order and
    // trade state as stale; do not reconcile here, because on reconnect it
    // runs before the subscription is restored and the server does not replay
    // events missed in between.
    using UserStreamGapCallback = std::function<void()>;
    // Fired once the authenticated subscription has been resent after a gap.
    // Reconcile order and trade state via REST (`ClobClient::get_open_orders`,
    // `get_trades`) here and merge it with events delivered from this point.
    using UserStreamRecoveredCallback = std::function<void()>;
    // Fired when the server rejects the session (close code 1008, e.g. invalid
    // API credentials). The stream stops reconnecting; call connect() to retry.
    using UserStreamErrorCallback = std::function<void(const std::string &error)>;

    // Authenticated user-channel stream. Subscriptions are replayed with the
    // API credentials after every reconnect.
    class UserStream
    {
    public:
        UserStream(const Config &config, const ApiCredentials &credentials);
        ~UserStream();

        UserStream(const UserStream &) = delete;
        UserStream &operator=(const UserStream &) = delete;

        // Receive events for every market the API key trades.
        void subscribe_all_markets();
        // Receive events only for these condition IDs. Ignored for markets
        // already covered by subscribe_all_markets().
        void subscribe(const std::vector<std::string> &condition_ids);
        void subscribe(const std::string &condition_id);
        void unsubscribe(const std::vector<std::string> &condition_ids);
        void unsubscribe(const std::string &condition_id);
        void unsubscribe_all();

        bool is_subscribed_to_all_markets() const;
        std::vector<std::string> subscribed_markets() const;

        // Callbacks
        void on_order(UserOrderCallback callback);
        void on_trade(UserTradeCallback callback);
        void on_stream_gap(UserStreamGapCallback callback);
        void on_stream_recovered(UserStreamRecoveredCallback callback);
        void on_error(UserStreamErrorCallback callback);

        // Connection
        bool connect();
        void disconnect();
        bool is_connected() const;
        bool authentication_failed() const;

        // Run event loop (blocking)
        void run();

        // Stop
        void stop();

        // Statistics
        uint64_t order_events() const;
        uint64_t trade_events() const;

    private:
        std::shared_ptr<detail::UserStreamRuntime> runtime_;
    };

} // namespace polymarket
