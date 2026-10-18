#include "gamma_parsing.hpp"

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

// Field names follow py-sdk models/gamma/comment.py, profile.py and search.py.
namespace polymarket::detail
{
    namespace
    {
        std::string profile_wallet(const json &item)
        {
            if (item.contains("wallet")) return field_string(item, "wallet");
            return field_string(item, "proxyWallet");
        }

        std::optional<std::int64_t> integer_or_text(const json &item, const char *key)
        {
            const auto *value = find_field(item, key);
            if (value == nullptr || !value->is_string()) return field_integer(item, key);
            const auto &text = value->get_ref<const std::string &>();
            const auto digits = text.starts_with('-') ? text.substr(1) : text;
            if (digits.empty() || digits.size() > 18 ||
                digits.find_first_not_of("0123456789") != std::string::npos)
                throw_bad_field(key, "an integer");
            return std::stoll(text);
        }

        std::optional<GammaCommentProfile> comment_profile(const json &item, const char *key)
        {
            if (!item.contains(key) || item.at(key).is_null()) return std::nullopt;
            const auto &value = item.at(key);
            GammaCommentProfile profile;
            profile.name = field_string(value, "name");
            profile.pseudonym = field_string(value, "pseudonym");
            profile.bio = field_string(value, "bio");
            profile.wallet = profile_wallet(value);
            profile.base_address = field_string(value, "baseAddress");
            profile.profile_image = field_string(value, "profileImage");
            profile.display_username_public = field_bool(value, "displayUsernamePublic");
            profile.is_mod = field_bool(value, "isMod");
            profile.is_creator = field_bool(value, "isCreator");
            for (const auto &position : field_objects(value, "positions"))
                profile.positions.push_back(
                    {field_id(position, "tokenId"), integer_or_text(position, "positionSize")});
            return profile;
        }

        GammaSearchProfile search_profile(const json &item)
        {
            GammaSearchProfile profile;
            profile.id = field_id(item, "id");
            profile.wallet = profile_wallet(item);
            profile.name = field_string(item, "name");
            profile.pseudonym = field_string(item, "pseudonym");
            profile.bio = field_string(item, "bio");
            profile.profile_image = field_string(item, "profileImage");
            profile.x_username = field_string(item, "xUsername");
            profile.display_username_public = field_bool(item, "displayUsernamePublic");
            profile.verified_badge = field_bool(item, "verifiedBadge");
            profile.created_at = field_string(item, "createdAt");
            profile.raw = item;
            return profile;
        }

        // `key` must be an array or null when present.
        std::vector<json> optional_array(const json &object, const char *key)
        {
            if (!object.contains(key) || object.at(key).is_null()) return {};
            if (!object.at(key).is_array())
                throw std::invalid_argument(std::string("Gamma field ") + key +
                                            " must be an array or null");
            return std::vector<json>(object.at(key).begin(), object.at(key).end());
        }
    } // namespace

    GammaReaction parse_gamma_reaction(const json &item)
    {
        GammaReaction result;
        result.id = field_required_id(item, "id");
        result.comment_id = field_integer(item, "commentID");
        result.reaction_type = field_string(item, "reactionType");
        result.icon = field_string(item, "icon");
        result.user_address = field_string(item, "userAddress");
        result.created_at = field_string(item, "createdAt");
        result.profile = comment_profile(item, "profile");
        return result;
    }

    GammaComment parse_gamma_comment(const json &item)
    {
        GammaComment comment;
        comment.id = field_required_id(item, "id");
        comment.body = field_string(item, "body");
        comment.parent_entity_type = field_string(item, "parentEntityType");
        comment.parent_entity_id = field_id(item, "parentEntityID");
        comment.parent_comment_id = field_id(item, "parentCommentID");
        comment.user_address = field_string(item, "userAddress");
        comment.reply_address = field_string(item, "replyAddress");
        comment.created_at = field_string(item, "createdAt");
        comment.updated_at = field_string(item, "updatedAt");
        comment.profile = comment_profile(item, "profile");
        for (const auto &value : field_objects(item, "reactions"))
            comment.reactions.push_back(parse_gamma_reaction(value));
        comment.report_count = field_integer(item, "reportCount");
        comment.reaction_count = field_integer(item, "reactionCount");
        comment.trade_asset = field_string(item, "tradeAsset");
        comment.raw = item;
        return comment;
    }

    std::vector<GammaComment> parse_gamma_comments(const std::string &body)
    {
        const auto parsed = json::parse(body);
        if (!parsed.is_array())
            throw std::invalid_argument("Gamma comments response must be an array");
        std::vector<GammaComment> comments;
        comments.reserve(parsed.size());
        for (const auto &item : parsed)
            comments.push_back(parse_gamma_comment(item));
        return comments;
    }

    Page<GammaComment> parse_gamma_comment_root_page(const std::string &body)
    {
        Page<GammaComment> page;
        page.items = parse_gamma_comments(body);
        std::size_t roots = 0;
        for (const auto &comment : page.items)
        {
            if (comment.parent_comment_id.empty()) ++roots;
        }
        page.counted = roots;
        return page;
    }

    GammaPublicProfile parse_gamma_public_profile(const std::string &body)
    {
        const auto item = json::parse(body);
        GammaPublicProfile profile;
        profile.wallet = profile_wallet(item);
        profile.name = field_string(item, "name");
        profile.pseudonym = field_string(item, "pseudonym");
        profile.bio = field_string(item, "bio");
        profile.profile_image = field_string(item, "profileImage");
        profile.x_username = field_string(item, "xUsername");
        profile.display_username_public = field_bool(item, "displayUsernamePublic");
        profile.verified_badge = field_bool(item, "verifiedBadge");
        profile.created_at = field_string(item, "createdAt");
        for (const auto &user : field_objects(item, "users"))
            profile.users.push_back({field_required_id(user, "id"),
                                     field_bool(user, "communityMod"), field_bool(user, "creator"),
                                     field_bool(user, "mod")});
        profile.raw = item;
        return profile;
    }

    Page<GammaSearchResults> parse_gamma_search_page(const std::string &body)
    {
        const auto parsed = json::parse(body);
        if (!parsed.is_object())
            throw std::invalid_argument("Gamma search response must be an object");
        GammaSearchResults results;
        for (const auto &event : optional_array(parsed, "events"))
            results.events.push_back(parse_gamma_event(event));
        for (const auto &tag : optional_array(parsed, "tags"))
            results.tags.push_back({field_required_id(tag, "id"), field_string(tag, "label"),
                                    field_string(tag, "slug"), field_integer(tag, "event_count")});
        for (const auto &profile : optional_array(parsed, "profiles"))
            results.profiles.push_back(search_profile(profile));

        Page<GammaSearchResults> page;
        if (parsed.contains("pagination") && !parsed.at("pagination").is_null())
        {
            const auto &pagination = parsed.at("pagination");
            page.has_more = field_bool(pagination, "hasMore").value_or(false);
            results.total_results = field_integer(pagination, "totalResults");
        }
        page.items.push_back(std::move(results));
        return page;
    }
} // namespace polymarket::detail
