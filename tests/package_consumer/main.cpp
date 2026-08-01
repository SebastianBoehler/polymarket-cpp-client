#include <clob_client.hpp>
#include <evm_event_indexer.hpp>
#include <http_client.hpp>
#include <market_fetcher.hpp>
#include <oracle_watcher.hpp>
#include <orderbook.hpp>
#include <polymarket/version.hpp>
#include <websocket_client.hpp>

int main()
{
    polymarket::http_global_init();
    {
        polymarket::HttpClient client;
        const polymarket::Config config;
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
