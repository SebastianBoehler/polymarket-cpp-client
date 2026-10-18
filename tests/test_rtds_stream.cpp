#include "polymarket/rtds_stream.hpp"
#include "rtds_fixtures.hpp"
#include "websocket_test_server.hpp"

#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
    using namespace std::chrono_literals;
    using websocket_test::LocalWebSocketServer;

    constexpr const char *created_frame =
        R"({"action":"subscribe","subscriptions":[{"topic":"comments","type":"comment_created"}]})";
    constexpr const char *both_frame =
        R"({"action":"subscribe","subscriptions":[{"topic":"comments","type":"comment_created"},)"
        R"({"topic":"comments","type":"reaction_created"}]})";
    constexpr const char *reaction_frame =
        R"({"action":"subscribe","subscriptions":[{"topic":"comments","type":"reaction_created"}]})";
    constexpr const char *reaction_unsubscribe =
        R"({"action":"unsubscribe","subscriptions":[{"topic":"comments","type":"reaction_created"}]})";

    bool check(bool condition, const std::string &message)
    {
        if (!condition) std::cerr << "failed: " << message << '\n';
        return condition;
    }

    template <typename Predicate>
    bool wait_until(Predicate predicate, std::chrono::milliseconds timeout)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (predicate()) return true;
            std::this_thread::sleep_for(2ms);
        }
        return predicate();
    }

    polymarket::Config local_config(const std::string &url)
    {
        polymarket::Config config;
        config.rtds_ws_url = url;
        return config;
    }

    struct Recorder
    {
        std::mutex mutex;
        std::vector<std::string> types;

        polymarket::RtdsCommentCallback callback()
        {
            return [this](const polymarket::RtdsCommentEvent &event)
            {
                std::lock_guard lock(mutex);
                types.push_back(event.type);
            };
        }

        std::size_t size()
        {
            std::lock_guard lock(mutex);
            return types.size();
        }
    };

    bool test_subscriptions_and_delivery()
    {
        LocalWebSocketServer server;
        polymarket::RtdsStream stream(local_config(server.url()));
        Recorder all_comments;
        Recorder on_event;
        Recorder reactions;
        stream.subscribe_comments({}, all_comments.callback());
        polymarket::RtdsCommentFilter parent;
        parent.parent_entity_id = 45915;
        parent.types = {"comment_created", "reaction_created"};
        stream.subscribe_comments(parent, on_event.callback());

        const bool connected = stream.connect();
        const bool replayed = server.wait_for_message_count(both_frame, 1, 2s);

        polymarket::RtdsCommentFilter reaction_filter;
        reaction_filter.types = {"reaction_created"};
        const auto reaction_id = stream.subscribe_comments(reaction_filter, reactions.callback());
        const bool no_new_frame = !server.wait_for_message_count(reaction_frame, 1, 200ms);

        server.send_to_clients("");
        server.send_to_clients(rtds_fixtures::rtds_comment_created);
        server.send_to_clients(rtds_fixtures::rtds_reaction_created);
        server.send_to_clients(R"({"topic":"activity","type":"trades","payload":{}})");
        const bool delivered = wait_until(
            [&]
            { return all_comments.size() == 1 && on_event.size() == 1 && reactions.size() == 1; },
            2s);

        stream.unsubscribe(reaction_id);
        stream.unsubscribe(reaction_id);
        const bool kept = !server.wait_for_message_count(reaction_unsubscribe, 1, 200ms);
        stream.disconnect();
        return check(connected && replayed, "subscriptions added before connect must be sent") &&
               check(no_new_frame, "an already subscribed key must not be sent again") &&
               check(delivered, "each subscription must receive its matching events") &&
               check(stream.events() == 2 && stream.dropped_events() == 1,
                     "unknown topics must be dropped and the empty frame skipped") &&
               check(kept, "a key still used by another subscription must stay subscribed");
    }

    bool test_diffs_and_replay()
    {
        LocalWebSocketServer server;
        polymarket::RtdsStream stream(local_config(server.url()));
        std::atomic<int> recoveries{0};
        stream.on_stream_recovered([&] { recoveries++; });
        const bool connected = stream.connect();
        Recorder reactions;
        polymarket::RtdsCommentFilter reaction_filter;
        reaction_filter.types = {"reaction_created"};
        const auto id = stream.subscribe_comments(reaction_filter, reactions.callback());
        const bool subscribed = server.wait_for_message_count(reaction_frame, 1, 2s);
        Recorder comments;
        stream.subscribe_comments({}, comments.callback());
        const bool added = server.wait_for_message_count(created_frame, 1, 2s);
        stream.unsubscribe(id);
        const bool removed = server.wait_for_message_count(reaction_unsubscribe, 1, 2s);

        server.close_clients();
        const bool reconnected = server.wait_for_connections(2, 5s);
        const bool restored = server.wait_for_message_count(created_frame, 2, 5s);
        const bool recovered = wait_until([&] { return recoveries.load() == 2; }, 5s);
        server.send_to_clients(rtds_fixtures::rtds_reaction_created);
        server.send_to_clients(rtds_fixtures::rtds_comment_created);
        const bool resumed = wait_until([&] { return comments.size() == 1; }, 2s);
        stream.disconnect();
        return check(connected && subscribed && added && removed,
                     "changes on a live connection must send subscribe and unsubscribe diffs") &&
               check(reconnected && restored && recovered,
                     "a reconnect must restore the combined subscription") &&
               check(resumed && reactions.size() == 0,
                     "a removed subscription must stop receiving events");
    }

    bool test_invalid_arguments()
    {
        polymarket::RtdsStream stream;
        polymarket::RtdsCommentFilter bad;
        bad.types = {"comment_edited"};
        bool rejected = false;
        try
        {
            stream.subscribe_comments(bad, {});
        }
        catch (const std::invalid_argument &)
        {
            rejected = true;
        }
        polymarket::Config empty;
        empty.rtds_ws_url.clear();
        bool empty_rejected = false;
        try
        {
            polymarket::RtdsStream invalid(empty);
        }
        catch (const std::invalid_argument &)
        {
            empty_rejected = true;
        }
        return check(rejected && empty_rejected, "invalid filters and URLs must be rejected");
    }
} // namespace

int main()
{
    const bool passed =
        test_subscriptions_and_delivery() && test_diffs_and_replay() && test_invalid_arguments();
    if (!passed) return 1;
    std::cout << "test_rtds_stream passed\n";
    return 0;
}
