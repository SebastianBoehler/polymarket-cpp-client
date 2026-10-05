#include <clob_client.hpp>
#include <evm_event_indexer.hpp>
#include <evm_abi.hpp>
#include <evm_transaction.hpp>
#include <http_client.hpp>
#include <market_fetcher.hpp>
#include <oracle_watcher.hpp>
#include <orderbook.hpp>
#include <polymarket/version.hpp>
#include <polymarket_contracts.hpp>
#include <position_client.hpp>
#include <user_stream.hpp>
#include <websocket_client.hpp>

#include <stdexcept>

int main()
{
    polymarket::http_global_init();
    {
        polymarket::HttpClient client;
        const polymarket::Config config;
        const polymarket::PositionClientConfig position_config;
        if (position_config.contracts.chain_id != 137 ||
            polymarket::evm_function_selector("transfer(address,uint256)") != "0xa9059cbb")
            return 1;
        // The installed stream must link and reject absent credentials without connecting.
        try
        {
            polymarket::UserStream stream(config, {});
            return 1;
        }
        catch (const std::invalid_argument &)
        {
        }
        const polymarket::EvmEventIndexerConfig evm_config;
        const polymarket::OracleResolutionState oracle_state;
        const polymarket::WebSocketOptions websocket_options;
        if (client.options().timeout_ms <= 0 || config.clob_ws_url.empty() ||
            evm_config.batch_size == 0 || oracle_state.events_seen != 0 ||
            websocket_options.message_queue_limit == 0 ||
            polymarket::version_major <= 0)
            return 1;
    }
    polymarket::http_global_cleanup();
    return 0;
}
