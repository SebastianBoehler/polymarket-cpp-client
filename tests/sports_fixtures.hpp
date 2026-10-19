#pragma once

// Frames recorded from wss://sports-api.polymarket.com/ws on 2026-10-18. They
// carry public game data only. sports_unscored_result (a UFC bout without a
// score) and sports_partial_result (a cricket update without gameId or status)
// are rejected, as both official SDKs reject them.
namespace sports_fixtures
{
    inline constexpr const char *sports_live_result =
        R"json({"gameId":70918912,"sportradarGameId":"568982f7-6051-4a30-8e7c-d9d6235f9472","leagueAbbreviation":"cfb","turn":"nd","homeTeam":"BYU","awayTeam":"ND","turnProviderId":"d7f19bff-74e7-4b71-a221-12dfeb144fd8","status":"inprogress","score":"10-21","elapsed":"00:33","period":"Q2","live":true,"ended":false})json";
    inline constexpr const char *sports_camel_status_result =
        R"json({"gameId":90104527,"leagueAbbreviation":"mls","homeTeam":"Atlanta United FC","awayTeam":"Inter Miami CF","status":"InProgress","score":"0-3","elapsed":"69","period":"2H","live":true,"ended":false})json";
    inline constexpr const char *sports_unscored_result =
        R"json({"gameId":10699,"leagueAbbreviation":"ufc","homeTeam":"Kyle Daukaus","awayTeam":"Marc-Andre Barriault","status":"In Progress","elapsed":"02:25","period":"R1","live":true,"ended":false})json";
    inline constexpr const char *sports_partial_result =
        R"json({"metadataGameId":"id2704651975152240","leagueAbbreviation":"cricket","score":"70-199","period":"Live","live":true,"ended":false})json";
} // namespace sports_fixtures
