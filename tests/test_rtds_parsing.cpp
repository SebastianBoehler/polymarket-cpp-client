#include "check_support.hpp"
#include "rtds_fixtures.hpp"
#include "rtds_parsing.hpp"

#include <functional>
#include <stdexcept>
#include <string>

namespace
{
    using check_support::check;
    using check_support::expect_equal;
    namespace detail = polymarket::detail;

    void expect_rejected(const std::string &name, const std::function<void()> &parse)
    {
        try
        {
            parse();
        }
        catch (const std::exception &)
        {
            return;
        }
        check(false, name + " must be rejected");
    }

    polymarket::RtdsCommentEvent comment_event(const char *frame)
    {
        return *detail::parse_rtds_message(frame)->comment;
    }

    void test_recorded_frames()
    {
        const auto created = comment_event(rtds_fixtures::rtds_comment_created);
        check(created.type == "comment_created" && created.timestamp_ms == 1792286429215 &&
                  created.comment && !created.reaction && created.comment->id == "3297168" &&
                  created.comment->parent_entity_id == "45915" &&
                  created.comment->parent_entity_type == "Event" &&
                  created.comment->body == "Example comment body." && created.comment->profile &&
                  created.comment->profile->wallet ==
                      "0x1111111111111111111111111111111111111111" &&
                  created.raw.contains("connection_id"),
              "a recorded comment must parse");

        const auto reaction = comment_event(rtds_fixtures::rtds_reaction_created);
        check(reaction.type == "reaction_created" && reaction.reaction && !reaction.comment &&
                  reaction.reaction->id == "3157076" && reaction.reaction->comment_id == 3297136 &&
                  reaction.reaction->reaction_type == "HEART",
              "a recorded reaction must parse");

        const auto removed = comment_event(rtds_fixtures::rtds_comment_removed);
        check(removed.type == "comment_removed" && removed.comment &&
                  removed.timestamp_ms == 1792294800000,
              "a removed comment must parse an ISO timestamp");

        check(!detail::parse_rtds_message(""), "the empty frame after connecting is skipped");

        auto with_position = nlohmann::json::parse(rtds_fixtures::rtds_comment_created);
        with_position["payload"]["profile"]["positions"] = {
            {{"positionSize", "47612999494"}, {"tokenId", "99"}}};
        const auto positioned = comment_event(with_position.dump().c_str());
        check(positioned.comment->profile->positions.at(0).position_size == 47612999494,
              "a live comment with a text position size must parse");
    }

    void test_matching()
    {
        const auto created = comment_event(rtds_fixtures::rtds_comment_created);
        const auto reaction = comment_event(rtds_fixtures::rtds_reaction_created);
        const auto defaults = detail::normalized_comment_filter({});
        check(defaults.types == std::vector<std::string>{"comment_created"},
              "comment_created is the default type");
        check(detail::rtds_comment_matches(defaults, created) &&
                  !detail::rtds_comment_matches(defaults, reaction),
              "the default filter keeps only created comments");

        polymarket::RtdsCommentFilter both;
        both.types = {"reaction_created", "comment_created", "reaction_created"};
        const auto normalized = detail::normalized_comment_filter(both);
        check(normalized.types.size() == 2 && detail::rtds_comment_matches(normalized, reaction),
              "types must be deduplicated and matched");

        auto parent = both;
        parent.parent_entity_id = 45915;
        parent.parent_entity_type = "Event";
        const auto by_parent = detail::normalized_comment_filter(parent);
        check(detail::rtds_comment_matches(by_parent, created) &&
                  !detail::rtds_comment_matches(by_parent, reaction),
              "a parent filter keeps its comments and drops reactions");
        parent.parent_entity_id = 1;
        check(!detail::rtds_comment_matches(detail::normalized_comment_filter(parent), created),
              "another parent must not match");
        parent.parent_entity_id.reset();
        parent.parent_entity_type = "Market";
        check(!detail::rtds_comment_matches(detail::normalized_comment_filter(parent), created),
              "another parent type must not match");

        const auto keys = detail::rtds_comment_keys(normalized);
        expect_equal(
            "subscribe frame", detail::rtds_subscription_frame("subscribe", keys),
            R"({"action":"subscribe","subscriptions":[{"topic":"comments","type":"reaction_created"},)"
            R"({"topic":"comments","type":"comment_created"}]})");
        expect_equal(
            "filtered frame",
            detail::rtds_subscription_frame("unsubscribe", {{"t", "x", "{\"a\":1}"}}),
            R"({"action":"unsubscribe","subscriptions":[{"filters":"{\"a\":1}","topic":"t","type":"x"}]})");
    }

    void test_rejections()
    {
        polymarket::RtdsCommentFilter bad_type;
        bad_type.types = {"comment_edited"};
        polymarket::RtdsCommentFilter bad_parent;
        bad_parent.parent_entity_type = "Series";
        expect_rejected("an unknown comment type",
                        [&] { (void)detail::normalized_comment_filter(bad_type); });
        expect_rejected("an unknown parent type",
                        [&] { (void)detail::normalized_comment_filter(bad_parent); });

        expect_rejected("non-JSON text", [] { (void)detail::parse_rtds_message("PONG"); });
        expect_rejected("an unknown topic",
                        []
                        {
                            (void)detail::parse_rtds_message(
                                R"({"topic":"activity","type":"trades","payload":{}})");
                        });
        expect_rejected(
            "an unknown comment type",
            []
            {
                (void)detail::parse_rtds_message(
                    R"({"topic":"comments","type":"comment_edited","payload":{"id":"1"}})");
            });
        expect_rejected("a missing payload",
                        []
                        {
                            (void)detail::parse_rtds_message(
                                R"({"topic":"comments","type":"comment_created"})");
                        });
        expect_rejected(
            "a comment without an ID",
            []
            {
                (void)detail::parse_rtds_message(
                    R"({"topic":"comments","type":"comment_created","payload":{"body":"x"}})");
            });
        expect_rejected(
            "a boolean timestamp",
            []
            {
                (void)detail::parse_rtds_message(
                    R"({"topic":"comments","type":"reaction_created","timestamp":true,"payload":{"id":"1"}})");
            });
    }
} // namespace

int main()
{
    test_recorded_frames();
    test_matching();
    test_rejections();
    return check_support::finish("test_rtds_parsing");
}
