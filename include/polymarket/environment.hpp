#pragma once

#include "polymarket/polymarket_contracts.hpp"
#include <string>
#include <string_view>

namespace polymarket
{
    // Every endpoint and contract for one Polymarket deployment. production()
    // and preproduction() match the official py-sdk and ts-sdk presets. Copy a
    // preset and override fields to target a fork or a local server.
    //
    // preproduction() is not a testnet: it uses separate CLOB, Gamma, Data API
    // and relayer hosts, but runs on Polygon mainnet (chain 137) with the
    // production contracts, RPC and CLOB WebSocket hosts.
    struct Environment
    {
        std::string name;
        std::string rpc_url;           // Polygon JSON-RPC over HTTP(S)
        PolymarketContracts contracts; // contracts.chain_id is the chain ID

        std::string clob_url;
        std::string clob_market_ws_url;
        std::string clob_user_ws_url;
        std::string gamma_url;
        std::string data_url;
        std::string relayer_url;
        std::string rtds_ws_url;
        std::string sports_ws_url;

        int relayer_max_polls{100};
        long relayer_poll_interval_ms{2000};

        static Environment production();
        static Environment preproduction();
        // "production" or "preproduction"; throws std::invalid_argument otherwise.
        static Environment from_name(std::string_view name);

        // Throws std::invalid_argument naming the first invalid field.
        void validate() const;
    };

} // namespace polymarket
