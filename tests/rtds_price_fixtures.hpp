#pragma once

// Crypto frames recorded from wss://ws-live-data.polymarket.com on 2026-10-18, with
// the connection ID replaced. The equity frames are synthetic, built from the py-sdk
// models, because equity_prices was silent while markets were closed.
namespace rtds_fixtures
{
    inline constexpr const char *rtds_binance_update =
        R"json({"connection_id":"example-connection","payload":{"full_accuracy_value":"81633.21000000","symbol":"btcusdt","timestamp":1792287281000,"value":81633.21},"timestamp":1792287281124,"topic":"crypto_prices","type":"update"})json";
    inline constexpr const char *rtds_chainlink_update =
        R"json({"connection_id":"example-connection","payload":{"full_accuracy_value":"81529465877486595000000","symbol":"btc/usd","timestamp":1792287296000,"value":81529.4658774866},"timestamp":1792287296856,"topic":"crypto_prices_chainlink","type":"update"})json";
    inline constexpr const char *rtds_twap_thirty_update =
        R"json({"connection_id":"example-connection","payload":{"full_accuracy_value":"81529202885321459499008","symbol":"btc/usd","timestamp":1792287312000,"value":81529.20288532146,"window_s":30},"timestamp":1792287313142,"topic":"crypto_prices_twap_thirty","type":"update"})json";
    inline constexpr const char *rtds_twap_sixty_update =
        R"json({"connection_id":"example-connection","payload":{"full_accuracy_value":"81527437376226865446912","symbol":"btc/usd","timestamp":1792287328000,"value":81527.43737622687,"window_s":60},"timestamp":1792287329255,"topic":"crypto_prices_twap_sixty","type":"update"})json";
    inline constexpr const char *rtds_equity_update =
        R"json({"connection_id":"example-connection","payload":{"symbol":"AAPL","timestamp":1792330200000,"value":231.42,"full_accuracy_value":"231.4215","received_at":1792330200150,"is_carried_forward":false},"timestamp":1792330200180,"topic":"equity_prices","type":"update"})json";
    inline constexpr const char *rtds_equity_history =
        R"json({"connection_id":"example-connection","payload":{"symbol":"aapl","data":[{"timestamp":1792330140000,"value":231.3},{"timestamp":1792330170000,"value":"231.35"}]},"timestamp":1792330200100,"topic":"equity_prices","type":"subscribe"})json";
} // namespace rtds_fixtures
