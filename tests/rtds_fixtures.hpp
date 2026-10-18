#pragma once

// Frames recorded from wss://ws-live-data.polymarket.com on 2026-10-18 (comments
// topic). Comment bodies, names, pseudonyms, wallets, addresses, avatars and the
// connection ID are replaced with synthetic values. rtds_comment_removed is the
// created frame relabelled, with an ISO timestamp, since none arrived live.
namespace rtds_fixtures
{
    inline constexpr const char *rtds_comment_created =
        R"json({"connection_id":"example-connection","payload":{"body":"Example comment body.","createdAt":"2026-10-18T01:20:29.190359Z","id":"3297168","parentEntityID":45915,"parentEntityType":"Event","profile":{"baseAddress":"0x2222222222222222222222222222222222222222","displayUsernamePublic":true,"name":"Example Commenter","proxyWallet":"0x1111111111111111111111111111111111111111","pseudonym":"Example-Pseudonym"},"reactionCount":0,"reportCount":0,"userAddress":"0x2222222222222222222222222222222222222222"},"timestamp":1792286429215,"topic":"comments","type":"comment_created"})json";
    inline constexpr const char *rtds_reaction_created =
        R"json({"connection_id":"example-connection","payload":{"commentID":3297136,"createdAt":"2026-10-18T01:19:50.897177Z","id":"3157076","profile":{"baseAddress":"0x2222222222222222222222222222222222222222","displayUsernamePublic":true,"name":"Example Commenter","proxyWallet":"0x1111111111111111111111111111111111111111","pseudonym":"Example-Pseudonym"},"reactionType":"HEART","userAddress":"0x2222222222222222222222222222222222222222"},"timestamp":1792286390914,"topic":"comments","type":"reaction_created"})json";
    inline constexpr const char *rtds_comment_removed =
        R"json({"connection_id":"example-connection","payload":{"body":"Example comment body.","createdAt":"2026-10-18T01:20:29.190359Z","id":"3297168","parentEntityID":45915,"parentEntityType":"Event","profile":{"baseAddress":"0x2222222222222222222222222222222222222222","displayUsernamePublic":true,"name":"Example Commenter","proxyWallet":"0x1111111111111111111111111111111111111111","pseudonym":"Example-Pseudonym"},"reactionCount":0,"reportCount":0,"userAddress":"0x2222222222222222222222222222222222222222"},"timestamp":"2026-10-18T03:40:00Z","topic":"comments","type":"comment_removed"})json";
} // namespace rtds_fixtures
