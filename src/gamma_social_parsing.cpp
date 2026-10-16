#include "gamma_parsing.hpp"

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
            if (item.contains("wallet")) return gamma_string(item, "wallet");
            return gamma_string(item, "proxyWallet");
        }

        std::optional<GammaCommentProfile> comment_profile(const json &item, const char *key)
        {
            if (!item.contains(key) || item.at(key).is_null()) return std::nullopt;
            const auto &value = item.at(key);
            GammaCommentProfile profile;
            profile.name = gamma_string(value, "name");
            profile.pseudonym = gamma_string(value, "pseudonym");
            profile.bio = gamma_string(value, "bio");
            profile.wallet = profile_wallet(value);
            profile.base_address = gamma_string(value, "baseAddress");
            profile.profile_image = gamma_string(value, "profileImage");
            profile.display_username_public = gamma_bool(value, "displayUsernamePublic");
            profile.is_mod = gamma_bool(value, "isMod");
            profile.is_creator = gamma_bool(value, "isCreator");
            for (const auto &position : gamma_objects(value, "positions"))
                profile.positions.push_back(
                    {gamma_id(position, "tokenId"), gamma_integer(position, "positionSize")});
            return profile;
        }

        GammaReaction reaction(const json &item)
        {
            GammaReaction result;
            result.id = gamma_required_id(item, "id");
            result.comment_id = gamma_integer(item, "commentID");
            result.reaction_type = gamma_string(item, "reactionType");
            result.icon = gamma_string(item, "icon");
            result.user_address = gamma_string(item, "userAddress");
            result.created_at = gamma_string(item, "createdAt");
            result.profile = comment_profile(item, "profile");
            return result;
        }

        GammaSearchProfile search_profile(const json &item)
        {
            GammaSearchProfile profile;
            profile.id = gamma_id(item, "id");
            profile.wallet = profile_wallet(item);
            profile.name = gamma_string(item, "name");
            profile.pseudonym = gamma_string(item, "pseudonym");
            profile.bio = gamma_string(item, "bio");
            profile.profile_image = gamma_string(item, "profileImage");
            profile.x_username = gamma_string(item, "xUsername");
            profile.display_username_public = gamma_bool(item, "displayUsernamePublic");
            profile.verified_badge = gamma_bool(item, "verifiedBadge");
            profile.created_at = gamma_string(item, "createdAt");
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

    GammaComment parse_gamma_comment(const json &item)
    {
        GammaComment comment;
        comment.id = gamma_required_id(item, "id");
        comment.body = gamma_string(item, "body");
        comment.parent_entity_type = gamma_string(item, "parentEntityType");
        comment.parent_entity_id = gamma_id(item, "parentEntityID");
        comment.parent_comment_id = gamma_id(item, "parentCommentID");
        comment.user_address = gamma_string(item, "userAddress");
        comment.reply_address = gamma_string(item, "replyAddress");
        comment.created_at = gamma_string(item, "createdAt");
        comment.updated_at = gamma_string(item, "updatedAt");
        comment.profile = comment_profile(item, "profile");
        for (const auto &value : gamma_objects(item, "reactions"))
            comment.reactions.push_back(reaction(value));
        comment.report_count = gamma_integer(item, "reportCount");
        comment.reaction_count = gamma_integer(item, "reactionCount");
        comment.trade_asset = gamma_string(item, "tradeAsset");
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
        profile.name = gamma_string(item, "name");
        profile.pseudonym = gamma_string(item, "pseudonym");
        profile.bio = gamma_string(item, "bio");
        profile.profile_image = gamma_string(item, "profileImage");
        profile.x_username = gamma_string(item, "xUsername");
        profile.display_username_public = gamma_bool(item, "displayUsernamePublic");
        profile.verified_badge = gamma_bool(item, "verifiedBadge");
        profile.created_at = gamma_string(item, "createdAt");
        for (const auto &user : gamma_objects(item, "users"))
            profile.users.push_back({gamma_required_id(user, "id"),
                                     gamma_bool(user, "communityMod"), gamma_bool(user, "creator"),
                                     gamma_bool(user, "mod")});
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
            results.tags.push_back({gamma_required_id(tag, "id"), gamma_string(tag, "label"),
                                    gamma_string(tag, "slug"), gamma_integer(tag, "event_count")});
        for (const auto &profile : optional_array(parsed, "profiles"))
            results.profiles.push_back(search_profile(profile));

        Page<GammaSearchResults> page;
        if (parsed.contains("pagination") && !parsed.at("pagination").is_null())
        {
            const auto &pagination = parsed.at("pagination");
            page.has_more = gamma_bool(pagination, "hasMore").value_or(false);
            results.total_results = gamma_integer(pagination, "totalResults");
        }
        page.items.push_back(std::move(results));
        return page;
    }
} // namespace polymarket::detail
