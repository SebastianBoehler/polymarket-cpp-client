#include "rtds_parsing.hpp"

#include "gamma_parsing.hpp"
#include "stream_time.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace polymarket::detail
{
    namespace
    {
        constexpr const char *comments_topic = "comments";

        bool is_comment_type(const std::string &type)
        {
            return type == "comment_created" || type == "comment_removed" ||
                   type == "reaction_created" || type == "reaction_removed";
        }

        const json &payload_object(const json &frame)
        {
            const auto *payload = find_field(frame, "payload");
            if (payload == nullptr || !payload->is_object())
                throw_bad_field("payload", "an object");
            return *payload;
        }
    } // namespace

    RtdsCommentEvent parse_rtds_comment_event(const json &frame)
    {
        RtdsCommentEvent event;
        event.type = field_required_string(frame, "type");
        if (!is_comment_type(event.type)) throw_bad_field("type", "a comment event type");
        event.timestamp_ms = stream_epoch_ms_or_iso(frame, "timestamp");
        const auto &payload = payload_object(frame);
        if (event.type.starts_with("comment_"))
            event.comment = parse_gamma_comment(payload);
        else
            event.reaction = parse_gamma_reaction(payload);
        event.raw = frame;
        return event;
    }

    std::optional<RtdsParsedEvent> parse_rtds_message(const std::string &message)
    {
        if (message.empty()) return std::nullopt;
        const auto frame = json::parse(message);
        RtdsParsedEvent event;
        event.topic = field_required_string(frame, "topic");
        event.type = field_required_string(frame, "type");
        if (event.topic == comments_topic)
            event.comment = parse_rtds_comment_event(frame);
        else
            event.price = parse_rtds_price_event(frame, event.topic);
        return event;
    }

    bool rtds_supported_frame(const std::string &message)
    {
        const auto frame = json::parse(message, nullptr, false);
        if (!frame.is_object()) return false;
        const auto *topic = find_field(frame, "topic");
        if (topic == nullptr || !topic->is_string()) return false;
        const auto &name = topic->get_ref<const std::string &>();
        return name == comments_topic || is_rtds_price_wire_topic(name);
    }

    std::string rtds_subscription_frame(const char *action, const std::vector<RtdsServerKey> &keys)
    {
        json subscriptions = json::array();
        for (const auto &key : keys)
        {
            json subscription = {{"topic", key.topic}, {"type", key.type}};
            if (!key.filters.empty()) subscription["filters"] = key.filters;
            subscriptions.push_back(std::move(subscription));
        }
        return json{{"action", action}, {"subscriptions", std::move(subscriptions)}}.dump();
    }

    RtdsCommentFilter normalized_comment_filter(const RtdsCommentFilter &filter)
    {
        RtdsCommentFilter normalized;
        for (const auto &type : filter.types)
        {
            if (!is_comment_type(type))
                throw std::invalid_argument("invalid comments event type: " + type);
            if (std::find(normalized.types.begin(), normalized.types.end(), type) ==
                normalized.types.end())
                normalized.types.push_back(type);
        }
        if (normalized.types.empty()) normalized.types.push_back("comment_created");
        if (!filter.parent_entity_type.empty() && filter.parent_entity_type != "Event" &&
            filter.parent_entity_type != "Market")
            throw std::invalid_argument("parent_entity_type must be Event or Market");
        normalized.parent_entity_id = filter.parent_entity_id;
        normalized.parent_entity_type = filter.parent_entity_type;
        return normalized;
    }

    std::vector<RtdsServerKey> rtds_comment_keys(const RtdsCommentFilter &normalized)
    {
        std::vector<RtdsServerKey> keys;
        keys.reserve(normalized.types.size());
        for (const auto &type : normalized.types)
            keys.push_back({comments_topic, type, ""});
        return keys;
    }

    bool rtds_comment_matches(const RtdsCommentFilter &normalized, const RtdsCommentEvent &event)
    {
        if (std::find(normalized.types.begin(), normalized.types.end(), event.type) ==
            normalized.types.end())
            return false;
        const bool parent_filter =
            normalized.parent_entity_id.has_value() || !normalized.parent_entity_type.empty();
        if (!parent_filter) return true;
        if (!event.comment) return false;
        if (normalized.parent_entity_id &&
            event.comment->parent_entity_id != std::to_string(*normalized.parent_entity_id))
            return false;
        return normalized.parent_entity_type.empty() ||
               event.comment->parent_entity_type == normalized.parent_entity_type;
    }
} // namespace polymarket::detail
