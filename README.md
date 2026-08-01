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

- CMake 3.22+
- C++20 compiler
- libcurl, OpenSSL, zlib
- Prebuilt release targets: macOS 12+ arm64 and Linux x86-64

## Installation

### Option 1: CMake FetchContent (Recommended)

Add to your `CMakeLists.txt`:

```cmake
include(FetchContent)

# Fetch specific version
FetchContent_Declare(
    polymarket_client
    GIT_REPOSITORY https://github.com/SebastianBoehler/polymarket-cpp-client.git
    GIT_TAG v2.0.0  # or any release tag
)
FetchContent_MakeAvailable(polymarket_client)

# Link to your target
target_link_libraries(your_target PRIVATE polymarket::client)
```

### Option 2: Pre-built Releases

Download pre-built binaries from [Releases](https://github.com/SebastianBoehler/polymarket-cpp-client/releases):

```bash
# macOS
curl -LO https://github.com/SebastianBoehler/polymarket-cpp-client/releases/download/v2.0.0/polymarket-cpp-client-macos-arm64.tar.gz
mkdir -p polymarket-cpp-client-2.0.0
tar -xzf polymarket-cpp-client-macos-arm64.tar.gz -C polymarket-cpp-client-2.0.0

# Linux
curl -LO https://github.com/SebastianBoehler/polymarket-cpp-client/releases/download/v2.0.0/polymarket-cpp-client-linux-x64.tar.gz
mkdir -p polymarket-cpp-client-2.0.0
tar -xzf polymarket-cpp-client-linux-x64.tar.gz -C polymarket-cpp-client-2.0.0
```

Keep this as a dedicated prefix because the archive contains its pinned static
dependencies and headers. Then point CMake at it:

```cmake
# Configure with:
# cmake -S . -B build -DCMAKE_PREFIX_PATH=/absolute/path/to/polymarket-cpp-client-2.0.0
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

### v2 migration

Version 2 is not binary-compatible with v1: several public concrete types grew
to support safe concurrency, stream recovery, metadata caching, and resumable
indexing. Recompile consumers against the v2 headers and archive. `OrderSigner`
is now explicitly non-copyable (moving remains supported), and balance/allowance
methods accept an optional conditional-token ID. `BalanceAllowance::allowances`
is now a per-spender map matching the V2 response. `RewardsInfo` and
`EarningsInfo` now expose V2 reward configs, market metadata, addresses, and
rates; the invented v1 `reward_epoch` and `epoch` fields were removed.
`Position` now includes the Data API's total/realized PnL, icon, event slug,
and opposite-outcome fields. Market-list methods now return `ClobMarketPage`
so callers retain `next_cursor`; read markets from its `data` member.
`create_market_order` now returns `PreparedOrder`, which keeps the FAK/FOK
execution policy beside the signature, and posting a raw `SignedOrder` requires
an explicit `OrderType`; its no-type overload defaults to FAK, matching the
combined create-and-post helper. Unsupported order-type enum values throw
instead of silently becoming GTC. `OrderResponse` now retains asynchronous
`trade_ids`, and `OpenOrder` retains owner/maker identities, associated trades,
and outcome. `Trade` now matches the V2 response, including nested
maker orders, owner/maker identities, bucket index, trader side, and optional
error information. The no-op `Config::max_combined` and `Config::size_usdc`
members were removed. `get_fee_rate` now requires a token ID;
`get_rewards_markets()` is replaced by `get_rewards_markets_current()` or the
condition-ID overload; and `Notification` now carries numeric `type`, `owner`,
and structured `payload` fields. Authentication now requires both signer and
API credentials, and an empty order tick size resolves market metadata.

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
- `polymarket_arb`: discover one crypto/neg-risk market and analyze hypothetical complementary YES/NO FOK batches

Build them with `POLYMARKET_CLIENT_BUILD_EXAMPLES=ON` and run from `build/`.
Local benchmark targets are documented in [docs/benchmarks.md](docs/benchmarks.md).

The arbitrage example defaults to a BTC 15-minute market and dry-run mode:

```bash
./build/polymarket_arb --15m --symbol btc --fetch-only
./build/polymarket_arb --neg-risk --max 5 --dry-run
```

`polymarket_arb` is analysis-only. It rejects `--live` before authentication or
network access because the CLOB batch endpoint processes orders independently;
two complementary FOK orders are not an atomic trade and can leave directional
exposure. `order_test --live` remains available for an explicitly requested
single-order smoke test, not paired arbitrage execution.

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

Indexer delivery is at-least-once across process restarts. The cursor file
persists position, not recent log bodies, so synthetic orphan retractions are
available only for logs retained by the running process. Use confirmations and
an idempotent downstream store; rebuild from source logs if a restart spans a
reorg inside your unconfirmed window.

`OracleResolutionDashboard` retains the newest 1,024 canonical events per key
for rollback and timeline reconstruction while its aggregate counters cover all
accepted events. A removal at or before that compacted boundary is ignored; use
persistent source logs and rebuild the dashboard when deeper reorg recovery is
required.

## Tests

`test_utils` exercises basic utility helpers. `test_evm_events` covers EVM topic hashing, log filter serialization, and UMA/CTF event decoding. `test_evm_event_indexer` covers block range planning and file-backed cursors. Transport, order execution, typed error, signing, and WebSocket resilience tests are included when `POLYMARKET_CLIENT_BUILD_TESTS=ON`. Run via `ctest --test-dir build`.

`test_oracle_watcher` validates in-memory normalization against synthetic UMA/CTF fixtures.
`test_oracle_watcher_historical` is labeled `live` and only performs its
historical RPC smoke check when `POLYMARKET_RUN_LIVE_SMOKE=1` is set. Run it
explicitly with `POLYMARKET_RUN_LIVE_SMOKE=1 ctest --test-dir build -L live`.
It uses these defaults unless overridden:
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
amounts with Polymarket's tick-size precision rules. Leave `tick_size` empty to
resolve it from the client's market-metadata cache, or provide the exact cached
minimum tick size explicitly.

## WebSocket Resilience

`WebSocketClient` supports additive production-safety options for market-data
consumers: automatic reconnect backoff, ping interval, bounded message queue,
subscription replay, typed message callbacks, and counters for reconnects,
dropped messages, parse errors, last message time, messages, and bytes.

```cpp
polymarket::WebSocketClient ws;
ws.set_url("wss://ws-subscriptions-clob.polymarket.com/ws/market");
polymarket::WebSocketOptions options;
options.message_queue_limit = 4096;
options.min_backoff_ms = 250;
options.max_backoff_ms = 5000;
options.max_reconnect_attempts = 5;
ws.configure(options);

const std::string subscription = R"({
  "assets_ids":["yes-token-id","no-token-id"],
  "type":"market",
  "custom_feature_enabled":true
})";
ws.track_subscription(subscription); // Sent on connect and replayed on reconnect.

ws.on_message([](const std::string& raw) {
    // Raw callback remains available.
});
ws.on_typed_message([](const polymarket::TypedWebSocketMessage& msg) {
    if (msg.event_type == "book" || msg.event_type == "price_change") {
        // Current CLOB market-channel event for msg.asset_id.
    }
});
ws.connect();
```

Track a subscription before connecting when using `WebSocketClient` directly.
`OrderbookManager` tracks the current token set, sends real dynamic
subscribe/unsubscribe operations, and restores the set after reconnect.

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
