# Polymarket C++ Client

[![build](https://github.com/SebastianBoehler/polymarket-cpp-client/actions/workflows/build.yml/badge.svg)](https://github.com/SebastianBoehler/polymarket-cpp-client/actions/workflows/build.yml)
[![release](https://img.shields.io/github/v/release/SebastianBoehler/polymarket-cpp-client)](https://github.com/SebastianBoehler/polymarket-cpp-client/releases)

Reusable C++20 client for Polymarket: REST, WebSocket streaming, and order signing (EIP-712) with examples and tests.

## Features

- **REST**: market discovery, orderbook/price queries, auth key management, and trading endpoints.
- **Transport controls**: configurable libcurl timeouts, keepalive, connection reuse, proxy/user-agent, request metrics, and cumulative stats.
- **WebSocket**: orderbook streaming via IXWebSocket with reconnect, subscription replay, typed callbacks, and backpressure counters.
- **Signing**: CLOB V2 EIP-712 order signing (secp256k1, keccak).
- **Decimal math**: shared scaled-integer conversion for trading amounts.
- **Structured errors**: opt-in `Result<T>` APIs with typed SDK error classification.
- **EVM JSON-RPC**: Polygon HTTP catch-up and WebSocket subscriptions for logs, heads, and pending transaction hashes.
- **Resolution Events**: Decoders for UMA adapter and Conditional Tokens resolution/redemption logs.
- **Proxy Support**: HTTP/HTTPS proxy with authentication for geo-restricted access.
- **Neg-Risk Markets**: Automatic exchange selection for neg_risk markets.
- **Examples**: REST (`rest_example`), signing (`sign_example`), WebSocket (`ws_example`), onchain watchers.
- **Benchmarks and tests**: local benchmark targets plus `ctest` coverage.

## Requirements

- CMake 3.16+
- C++20 compiler
- libcurl, OpenSSL

## Installation

### Option 1: CMake FetchContent (Recommended)

Add to your `CMakeLists.txt`:

```cmake
include(FetchContent)

# Fetch specific version
FetchContent_Declare(
    polymarket_client
    GIT_REPOSITORY https://github.com/SebastianBoehler/polymarket-cpp-client.git
    GIT_TAG v1.2.4  # or any release tag
)
FetchContent_MakeAvailable(polymarket_client)

# Link to your target
target_link_libraries(your_target PRIVATE polymarket::client)
```

### Option 2: Pre-built Releases

Download pre-built binaries from [Releases](https://github.com/SebastianBoehler/polymarket-cpp-client/releases):

```bash
# macOS
curl -LO https://github.com/SebastianBoehler/polymarket-cpp-client/releases/download/v1.2.4/polymarket-cpp-client-macos-arm64.tar.gz
tar -xzf polymarket-cpp-client-macos-arm64.tar.gz -C /usr/local

# Linux
curl -LO https://github.com/SebastianBoehler/polymarket-cpp-client/releases/download/v1.2.4/polymarket-cpp-client-linux-x64.tar.gz
tar -xzf polymarket-cpp-client-linux-x64.tar.gz -C /usr/local
```

Then in your CMake:

```cmake
find_package(polymarket_client REQUIRED)
target_link_libraries(your_target PRIVATE polymarket::client)
```

### Option 3: Build from Source

```bash
cmake -S . -B build -DPOLYMARKET_CLIENT_BUILD_EXAMPLES=ON -DPOLYMARKET_CLIENT_BUILD_TESTS=ON
cmake --build build --parallel
# optional tests
ctest --test-dir build
# optional benchmarks
cmake -S . -B build -DPOLYMARKET_CLIENT_BUILD_BENCHMARKS=ON
# install (into system or a prefix you configure)
cmake --install build --prefix <install_prefix>
```

## Version Info

Check library version at runtime:

```cpp
#include <polymarket/version.hpp>
#include <iostream>

int main() {
    std::cout << "polymarket-cpp-client v" << polymarket::version_string << "\n";
    // Or access individual components:
    // polymarket::version_major, version_minor, version_patch
}
```

## Examples

- `rest_example`: fetch markets from CLOB REST
- `sign_example`: sign a dummy order (requires `PRIVATE_KEY`)
- `ws_example`: connect to Polymarket WS and subscribe to orderbook agg
- `uma_oracle_watch`: stream UMA adapter lifecycle events over Polygon JSON-RPC
- `condition_resolution_watch`: stream Conditional Tokens resolution/redemption events
- `evm_event_indexer_example`: persistent HTTP catch-up + live WS indexer with a cursor file
- `feed_latency_benchmark`: compare local receive timing across Polymarket market WS and Polygon RPC WS

Build them with `POLYMARKET_CLIENT_BUILD_EXAMPLES=ON` and run from `build/`.
Local benchmark targets are documented in [docs/benchmarks.md](docs/benchmarks.md).

## CLOB V2 Status

Polymarket production CLOB uses V2 at `https://clob.polymarket.com`. The
authenticated order path signs V2 EIP-712 orders, posts V2 order payloads, uses
the V2 exchange contracts, and supports the V2 `POLY_1271` deposit-wallet
signature wrapper. See [docs/clob-v2-migration.md](docs/clob-v2-migration.md)
for implementation notes.

## Structured Errors

Existing convenience methods still return `std::optional`, vectors, booleans,
or `OrderResponse`. New opt-in `Result<T>` methods expose structured failures
for callers that need to inspect transport errors, API responses, auth failures,
rate limits, parse errors, signing errors, and invalid arguments.

```cpp
auto result = client.get_open_orders_result();
if (!result) {
    const auto& error = result.error();
    std::cerr << polymarket::sdk_error_code_to_string(error.code)
              << " " << error.http_status
              << " " << error.message << "\n";
}
```

`SdkError` includes endpoint, HTTP status, response body excerpt, retryability,
and request id when the server returns a recognizable request-id header.

## Polygon JSON-RPC Watchers

The client includes provider-neutral EVM JSON-RPC helpers for users who want to build their own low-latency indexer instead of depending on a third-party Polymarket data feed.

```bash
# Live UMA adapter events
POLYMARKET_POLYGON_RPC_WS=wss://your-polygon-rpc \
POLYMARKET_UMA_CTF_ADAPTER=0x... \
./build/uma_oracle_watch

# Catch up historical CTF logs first, then stream live logs
./build/condition_resolution_watch \
  --rpc-http https://your-polygon-rpc \
  --rpc-ws wss://your-polygon-rpc \
  --ctf 0x... \
  --from-block 0x39f0000 \
  --to-block latest
```

Optional flags:

- `--pending`: also subscribe to `newPendingTransactions` if the RPC provider or node exposes mempool data.
- `--heads`: also stream new block headers.
- `--duration-seconds <n>`: stop automatically after `n` seconds.
- `--timeout-ms <n>`: override the HTTP JSON-RPC timeout used during catch-up.

Use current contract addresses from the official [Polymarket contracts docs](https://docs.polymarket.com/resources/contracts) and [resolution docs](https://docs.polymarket.com/concepts/resolution). The examples intentionally require addresses via args or env vars because Polymarket has versioned resolution contracts and docs may list both active and legacy adapters.

For persistent indexing, combine `eth_getLogs` catch-up (`--from-block`) with live `eth_subscribe` logs and store the last processed block in your own application. A normal hosted WebSocket RPC is enough for confirmed logs. Complete pre-confirmation mempool visibility is not guaranteed by public RPC; for that you typically need a provider that exposes pending transactions at scale or your own Polygon node with txpool/mempool access.

The reusable `EvmEventIndexer` handles the catch-up/live handoff for any `EvmLogFilter`:

```bash
./build/evm_event_indexer_example \
  --rpc-http https://your-polygon-rpc \
  --rpc-ws wss://your-polygon-rpc \
  --ctf 0x... \
  --cursor-file ./polymarket-cursors.json \
  --start-block 0x51b0000 \
  --live \
  --duration-seconds 60
```

If no cursor exists and `--start-block` is omitted, the example starts at the current latest block to avoid accidental full-history backfills. Pass `--start-block` explicitly when you want historical replay.

## Tests

`test_utils` exercises basic utility helpers. `test_evm_events` covers EVM topic hashing, log filter serialization, and UMA/CTF event decoding. `test_evm_event_indexer` covers block range planning and file-backed cursors. Transport, order execution, typed error, signing, and WebSocket resilience tests are included when `POLYMARKET_CLIENT_BUILD_TESTS=ON`. Run via `ctest --test-dir build`.

`test_oracle_watcher` validates in-memory normalization against synthetic UMA/CTF fixtures.
`test_oracle_watcher_historical` performs a live historical smoke check using defaults:
- `POLYMARKET_POLYGON_RPC_HTTP=https://polygon-bor.publicnode.com`
- `POLYMARKET_UMA_CTF_ADAPTER=0x6a9d222616c90fca5754cd1333cfd9b7fb6a4f74`
- `POLYMARKET_CONDITIONAL_TOKENS=0x4d97dcd97ec945f40cf65f87097ace5ea0476045`

Set these env vars explicitly if you need another RPC or contract set.

## Key components

- `include/` headers for client API
- `src/http_client.cpp`: libcurl HTTP client
- `src/sdk_error.cpp`: typed SDK error helpers
- `src/websocket_client.cpp`: IXWebSocket wrapper
- `src/websocket_client_resilience.cpp`: reconnect, queue, and subscription helpers
- `src/json_rpc_client.cpp`: EVM HTTP/WS JSON-RPC helpers
- `src/evm_event_indexer.cpp`: persistent log catch-up and live indexing
- `src/evm_utils.cpp`: ABI/log utilities
- `src/polymarket_events.cpp`: UMA/CTF event decoders
- `src/order_signer.cpp`: EIP-712 signing (secp256k1, keccak)
- `src/clob_client.cpp`: REST endpoints and parsing
- `src/clob_order_execution.cpp`: trading/order execution endpoints
- `src/orderbook.cpp`: WS orderbook management

## Proxy Configuration

Configure HTTP proxy for geo-restricted access:

```cpp
#include "clob_client.hpp"

polymarket::ClobClient client("https://clob.polymarket.com", 137);

// Set proxy (supports authentication)
client.set_proxy("http://user:pass@proxy.example.com:8080");

// Optional: set custom user agent
client.set_user_agent("Mozilla/5.0 (Windows NT 10.0; Win64; x64) ...");
```

## Low-Latency Trading (Keep TCP/TLS Hot)

Keep TCP/TLS connections warm by configuring transport options once, pre-warming
the connection, and optionally running a heartbeat:

```cpp
#include "clob_client.hpp"

polymarket::HttpClientOptions http_options;
http_options.timeout_ms = 2500;
http_options.connect_timeout_ms = 1000;
http_options.dns_cache_timeout_seconds = 120;
http_options.tcp_nodelay = true;
http_options.tcp_keepalive = true;

polymarket::ClobClient client("https://clob.polymarket.com", 137,
                               private_key, creds);
client.configure_transport(http_options);
client.warm_connection();
client.start_heartbeat(25);

auto response = client.create_and_post_order(params);
auto stats = client.get_connection_stats();
std::cout << "Avg latency: " << stats.avg_latency_ms << "ms\n";

client.stop_heartbeat();
```

## Order Precision

Limit and market order helpers round prices, share sizes, and maker/taker
amounts with Polymarket's tick-size precision rules. `CreateOrderParams` and
`CreateMarketOrderParams` default to `tick_size = "0.01"`; set this from cached
market metadata when trading markets with a different minimum tick size.

## WebSocket Resilience

`WebSocketClient` supports additive production-safety options for market-data
consumers: automatic reconnect backoff, ping interval, bounded message queue,
subscription replay, typed message callbacks, and counters for reconnects,
dropped messages, parse errors, last message time, messages, and bytes.

```cpp
polymarket::WebSocketClient ws;
polymarket::WebSocketOptions options;
options.message_queue_limit = 4096;
options.min_backoff_ms = 250;
options.max_backoff_ms = 5000;
ws.configure(options);

ws.on_message([](const std::string& raw) {
    // Raw callback remains available.
});
ws.on_typed_message([](const polymarket::TypedWebSocketMessage& msg) {
    if (msg.topic == "clob_market" && msg.type == "agg_orderbook") {
        // Use msg.asset_id and msg.payload.
    }
});
```

Call `track_subscription(subscription_json)` after sending a subscription if
you use `WebSocketClient` directly. `OrderbookManager` tracks its subscription
message and restores it automatically after reconnect.

## Neg-Risk Markets

The client automatically detects neg_risk markets and uses the appropriate exchange address for order signing:

- **Standard markets**: `0xE111180000d2663C0091e4f400237545B87B996B`
- **Neg-risk markets**: `0xe2222d279d744050d28e00520010520000310F59`

This is handled automatically in `create_order()` - no manual intervention needed.

## GitHub Actions

- **build.yml**: Debug and Release builds on Linux and macOS, with examples, tests, benchmark targets, and `ctest`.
- **release.yml**: Automated releases when you push a version tag

Push a version tag to trigger the release workflow, which builds macOS/Linux
artifacts and creates the GitHub release.

## License

MIT
