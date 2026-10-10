<div align="center">

# polymarket-cpp-client

**A low-latency C++20 SDK for Polymarket.**<br>
CLOB REST and WebSocket streaming, EIP-712 order signing, on-chain positions, and Polygon indexing.

[![build](https://github.com/SebastianBoehler/polymarket-cpp-client/actions/workflows/build.yml/badge.svg)](https://github.com/SebastianBoehler/polymarket-cpp-client/actions/workflows/build.yml)
[![release](https://img.shields.io/github/v/release/SebastianBoehler/polymarket-cpp-client)](https://github.com/SebastianBoehler/polymarket-cpp-client/releases)
[![license](https://img.shields.io/github/license/SebastianBoehler/polymarket-cpp-client)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus&logoColor=white)](https://en.cppreference.com/w/cpp/20)
[![CMake](https://img.shields.io/badge/CMake-3.22%2B-064F8C?logo=cmake&logoColor=white)](https://cmake.org)
[![platforms](https://img.shields.io/badge/platforms-Linux%20%7C%20macOS-lightgrey)](#requirements)
[![stars](https://img.shields.io/github/stars/SebastianBoehler/polymarket-cpp-client?style=flat&logo=github)](https://github.com/SebastianBoehler/polymarket-cpp-client/stargazers)
[![PRs welcome](https://img.shields.io/badge/PRs-welcome-brightgreen)](CONTRIBUTING.md)

[Quick start](#quick-start) ·
[Features](#features) ·
[Installation](#installation) ·
[Examples](#examples) ·
[Performance](#performance) ·
[Docs](#documentation) ·
[Contributing](#contributing)

</div>

---

## C++ prediction-market clients

Part of a collection of C++20 clients for prediction markets:

- [Polymarket](https://github.com/SebastianBoehler/polymarket-cpp-client)
- [Limitless Exchange](https://github.com/SebastianBoehler/limitless-cpp-client)
- [Opinion.trade](https://github.com/SebastianBoehler/opinion-cpp-client)

Explore the other clients for market data, order signing, and trading on each platform.

## Quick start

```cpp
#include <polymarket/clob_client.hpp>

#include <cstdlib>
#include <iostream>

int main()
{
    using namespace polymarket;
    const char *private_key = std::getenv("PRIVATE_KEY");
    if (!private_key) return 1;

    // Derive L2 API credentials from the signer once, then trade with both.
    const auto creds = ClobClient("https://clob.polymarket.com", 137, private_key)
                           .create_or_derive_api_key();
    ClobClient client("https://clob.polymarket.com", 137, private_key, creds);

    // Polymarket rejects orders from restricted regions; check before trading.
    const auto geoblock = client.get_geoblock_status();
    if (!geoblock || geoblock.value().blocked) return 1;

    client.warm_connection(); // open TCP + TLS before the first order

    PlaceLimitOrderParams order;
    order.token_id = "<token id>";
    order.price = 0.42;
    order.size = 10;
    order.side = OrderSide::BUY;

    const auto placed = client.place_limit_order(order); // GTC unless expiration is set
    std::cout << (placed ? placed.value().order_id : placed.error().message) << "\n";
}
```

Tick size and neg-risk metadata are resolved and cached for you. See
[Examples](#examples) for streaming, user events, and on-chain positions.

## Features

| Area                   | What you get                                                                                                                                       |
| ---------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Trading**            | CLOB V2 EIP-712 order signing (EOA, proxy, Safe, `POLY_1271` deposit wallets), limit and market orders, batch posting, cancels, tick-size rounding |
| **Order flow**         | Book-walk fill estimates, one-call limit and market orders with post-only, GTD, and worst-price bounds, on-chain settlement waits                  |
| **Market data**        | REST books, prices, midpoints, markets, trades; WebSocket orderbook streaming with reconnect, subscription replay, gap detection, backpressure     |
| **User stream**        | Authenticated `UserStream` with typed order/trade events, gap callbacks, and REST reconciliation hooks                                             |
| **On-chain positions** | `PositionClient` split/merge/redeem for CTF and Protocol V2, trading approvals, gasless Safe operations through the Polymarket relayer             |
| **Polygon indexing**   | JSON-RPC HTTP catch-up + WebSocket subscriptions, persistent `EvmEventIndexer`, UMA and Conditional Tokens event decoders                          |
| **Networking**         | HTTP/HTTPS/SOCKS proxies and VPN interface binding for REST **and** WebSockets, one process-wide route, fail-closed, geoblock eligibility check    |
| **Low latency**        | Warm keep-alive connections, heartbeat, `TCP_NODELAY`, DNS caching, metadata caches, per-request metrics                                           |
| **Errors**             | Opt-in `Result<T>` APIs with typed `SdkError` (transport, API, auth, rate limit, parse, signing, liquidity, timeout) and request IDs               |
| **Neg-risk markets**   | Automatic exchange and collateral-adapter selection                                                                                                |

## Requirements

- CMake 3.22+ and a C++20 compiler
- libcurl, OpenSSL, zlib
- Linux or macOS (prebuilt releases: Linux x86-64, macOS 12+ arm64)

Other dependencies (nlohmann/json, IXWebSocket, secp256k1, keccak) are fetched
and pinned by hash at configure time.

## Installation

### CMake FetchContent (recommended)

```cmake
include(FetchContent)
FetchContent_Declare(
    polymarket_client
    GIT_REPOSITORY https://github.com/SebastianBoehler/polymarket-cpp-client.git
    GIT_TAG v3.0.0 # or any release tag
)
FetchContent_MakeAvailable(polymarket_client)

target_link_libraries(your_target PRIVATE polymarket::client)
```

### Prebuilt releases

Download an archive from [Releases](https://github.com/SebastianBoehler/polymarket-cpp-client/releases)
and keep it as a dedicated prefix, since it contains its pinned static
dependencies and headers:

```bash
# macOS arm64 (use polymarket-cpp-client-linux-x64.tar.gz on Linux)
curl -LO https://github.com/SebastianBoehler/polymarket-cpp-client/releases/download/v3.0.0/polymarket-cpp-client-macos-arm64.tar.gz
mkdir -p polymarket-cpp-client-3.0.0
tar -xzf polymarket-cpp-client-macos-arm64.tar.gz -C polymarket-cpp-client-3.0.0
```

```cmake
# cmake -S . -B build -DCMAKE_PREFIX_PATH=/absolute/path/to/polymarket-cpp-client-3.0.0
find_package(polymarket_client REQUIRED)
target_link_libraries(your_target PRIVATE polymarket::client)
```

### From source

```bash
./build.sh            # configure, build examples and tests, run offline tests
# or step by step:
cmake -S . -B build -DPOLYMARKET_CLIENT_BUILD_EXAMPLES=ON -DPOLYMARKET_CLIENT_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure -LE live
cmake --install build --prefix <install_prefix>
```

Upgrading from v1 or v2? See the [migration guide](docs/migration.md). Check the
version at runtime with `polymarket::version_string` from `<polymarket/version.hpp>`.

## Usage

### Stream orderbooks

```cpp
#include <polymarket/websocket_client.hpp>

polymarket::WebSocketClient ws;
ws.set_url("wss://ws-subscriptions-clob.polymarket.com/ws/market");

polymarket::WebSocketOptions options;
options.message_queue_limit = 4096;
options.max_reconnect_attempts = 5;
ws.configure(options);

// Sent on connect and replayed after every reconnect.
ws.track_subscription(R"({"assets_ids":["<yes token>","<no token>"],"type":"market"})");
ws.on_typed_message([](const polymarket::TypedWebSocketMessage &msg) {
    if (msg.event_type == "book" || msg.event_type == "price_change") {
        // msg.asset_id changed
    }
});
ws.connect();
```

`OrderbookManager` builds on this: it keeps sorted books per token, sends real
subscribe/unsubscribe operations, and restores the token set after reconnect.
`WebSocketStats` counts reconnects, dropped messages, parse errors, and bytes.

### Keep connections warm

```cpp
polymarket::HttpClientOptions http;
http.timeout_ms = 2500;
http.connect_timeout_ms = 1000;
http.dns_cache_timeout_seconds = 120;

polymarket::ClobClient client("https://clob.polymarket.com", 137, private_key, creds,
                              polymarket::SignatureType::EOA, "", http);
client.warm_connection();
client.start_heartbeat(25);

auto response = client.create_and_post_order(params);
std::cout << "avg latency: " << client.get_connection_stats().avg_latency_ms << " ms\n";
```

Order helpers round prices, sizes, and maker/taker amounts to the market's tick
size. Leave `tick_size` empty to resolve it from the client's metadata cache.

### Preview, place, and settle orders

```cpp
using namespace polymarket;

// Walk the live book: worst price, average price, and shares for a $250 BUY.
auto estimate = client.estimate_market_price(token_id, OrderSide::BUY, 250.0);
if (!estimate) return; // InsufficientLiquidity when a FOK cannot fill

PlaceMarketOrderParams order;
order.token_id = token_id;
order.amount = 250.0;
order.worst_price = estimate.value().price; // never fill worse than the preview
auto placed = client.place_market_order(order); // FAK by default
if (!placed) return;

// Matched fills are final only once their transaction confirms on-chain.
auto settlement = client.wait_for_order_fill_settlement(placed.value());
```

`place_limit_order` posts GTC, or GTD when `expiration` is set, and supports
`post_only`. `estimate_market_price` also takes a book you already hold, such as
one from `OrderbookManager`, without a request. See
[docs/order-flow.md](docs/order-flow.md).

### Select an environment

`Environment` holds every endpoint and contract for one deployment. Pass it to
`ClobClient`, or build stream and position configs from it:

```cpp
#include <polymarket/environment.hpp>

const auto env = polymarket::Environment::preproduction(); // or production()
polymarket::ClobClient client(env, private_key, creds);
polymarket::UserStream stream(polymarket::Config::for_environment(env), creds);
auto position_config = polymarket::PositionClientConfig::for_environment(env);
```

`preproduction()` uses separate CLOB, Gamma, Data API, and relayer hosts, but
it is not a testnet. It runs on Polygon mainnet with the production contracts,
RPC, and CLOB WebSocket hosts, so on-chain operations and settled orders use
real funds. API keys are per environment. Copy a preset and override fields to
target a fork or a local server. The string constructors
(`ClobClient(base_url, 137, ...)`) override only the production CLOB host and
accept only chain 137.

### Route traffic through a proxy or VPN interface

```cpp
#include <polymarket/network.hpp>

// Every SDK connection, REST and WebSocket, now uses this route.
polymarket::set_default_network_route({.proxy_url = "socks5h://127.0.0.1:1080"});
// or bind to a VPN tunnel interface instead:
polymarket::set_default_network_route({.interface_name = "wg0"});
```

Routes fail closed: if the proxy or interface is unavailable, connections fail
instead of going direct. Per-client overrides live in `HttpClientOptions` and
`WebSocketOptions`. See [docs/networking.md](docs/networking.md).

> [!IMPORTANT]
> Proxies and VPNs change your network path, not your location. Polymarket's
> [Terms of Use](https://polymarket.com/tos) prohibit using them to get around
> geographic restrictions. Use `get_geoblock_status()` to check eligibility.

### Structured errors

Convenience methods return `std::optional`, vectors, booleans, or
`OrderResponse`. Opt-in `*_result` methods return `Result<T>` with a typed error:

```cpp
auto result = client.get_open_orders_result();
if (!result) {
    const auto &error = result.error();
    std::cerr << polymarket::sdk_error_code_to_string(error.code) << " "
              << error.http_status << " " << error.message << "\n";
}
```

`SdkError` includes the endpoint, HTTP status, response excerpt, retryability,
and the request ID when the server returns one. Rejections also carry
`retry_after_seconds` and the `Poly-RateLimit-*` state when the server sends them.

### Rate limits

Reads that get HTTP 429 are retried up to twice, each after exactly the
server's `Retry-After` delay (1 s when absent). A requested delay above 5 s
returns the error instead. Orders, cancellations, and other writes are never
retried; check `retry_after_seconds` on their error and decide yourself.

```cpp
client.set_rate_limit_retry(polymarket::RateLimitRetry{1, std::chrono::seconds(2)});
client.set_rate_limit_retry(std::nullopt); // fail on the first 429

client.set_rate_limit_listener([](const polymarket::RateLimitUpdate &update) {
    if (update.warning) std::cerr << "order rate would be rejected under enforcement\n";
});
```

The listener receives the `Poly-RateLimit-*` headers from order and cancel
responses. `PositionClientConfig::rate_limit_retry` and `Config::rate_limit_retry`
set the same policy for `PositionClient` and `MarketFetcher`. Unlike the official
SDKs, CLOB reads are retried too; disable it for latency-critical paths.

### On-chain positions

```cpp
#include <polymarket/position_client.hpp>

polymarket::PositionClientConfig config;
config.private_key = std::getenv("PRIVATE_KEY");
config.rpc_url = std::getenv("POLYGON_RPC_ENDPOINT");
config.wallet_type = polymarket::SignatureType::POLY_GNOSIS_SAFE; // or EOA
config.relayer_api_key = std::getenv("RELAYER_API_KEY");

polymarket::PositionClient positions(config);
positions.setup_trading_approvals();            // once per wallet; grants what is missing
positions.merge_positions(condition_id, "max").wait();
```

See [docs/position-operations.md](docs/position-operations.md) for wallet types,
batching and atomicity, errors, and the approval set.

### Neg-risk markets

`create_order()` detects neg-risk markets and signs against the matching
exchange; `PositionClient` picks the neg-risk collateral adapter the same way.

- Standard exchange: `0xE111180000d2663C0091e4f400237545B87B996B`
- Neg-risk exchange: `0xe2222d279d744050d28e00520010520000310F59`

## Examples

Build with `-DPOLYMARKET_CLIENT_BUILD_EXAMPLES=ON` and run from `build/`.

| Example                      | What it does                                                                                 |
| ---------------------------- | -------------------------------------------------------------------------------------------- |
| `rest_example`               | Public markets and books; balances and open orders with credentials                          |
| `sign_example`               | Signs a dummy order (`PRIVATE_KEY`)                                                          |
| `ws_example`                 | Streams market-channel orderbook messages                                                    |
| `user_stream_example`        | Streams your own order and trade events (`PRIVATE_KEY`)                                      |
| `order_flow_example`         | Estimates, places, and settles a market or post-only limit order; dry run unless `--execute` |
| `position_example`           | Split, merge, or redeem from an EOA or Safe; dry run unless `--execute`                      |
| `approvals_example`          | Lists and grants missing trading approvals; dry run unless `--execute`                       |
| `uma_oracle_watch`           | Streams UMA adapter lifecycle events over Polygon JSON-RPC                                   |
| `condition_resolution_watch` | Streams Conditional Tokens resolution and redemption events                                  |
| `evm_event_indexer_example`  | Persistent HTTP catch-up + live WebSocket indexer with a cursor file                         |
| `feed_latency_benchmark`     | Compares receive timing across the Polymarket market WS and a Polygon RPC WS                 |
| `polymarket_arb`             | Analysis-only scan of complementary YES/NO books (`--15m --symbol btc --fetch-only`)         |

Examples that call Polymarket services, and `order_test`, read
`POLYMARKET_ENV` (`production` by default, or `preproduction`).

`polymarket_arb` rejects `--live`: the CLOB batch endpoint processes orders
independently, so two complementary FOK orders are not an atomic trade.

## Performance

Hot paths are measured by small benchmark targets
(`-DPOLYMARKET_CLIENT_BUILD_BENCHMARKS=ON`). Best of 7 runs, Release build,
Apple M4 Max, real-length 77-digit token IDs:

| Workload                                        | Time per operation |
| ----------------------------------------------- | ------------------ |
| Parse + apply a 2-change `price_change` message | 3.4 µs             |
| Parse + apply a 100-level book snapshot         | 28.7 µs            |
| Sign a CLOB V2 order (EIP-712, secp256k1)       | 15.6 µs            |

Methodology and how to compare branches: [docs/benchmarks.md](docs/benchmarks.md).

## Documentation

| Guide                                                | Topic                                                  |
| ---------------------------------------------------- | ------------------------------------------------------ |
| [Order flow](docs/order-flow.md)                     | Fill estimates, one-call orders, settlement waits      |
| [Networking](docs/networking.md)                     | Proxies, VPN interfaces, WebSocket routing, geoblock   |
| [Position operations](docs/position-operations.md)   | Split, merge, redeem, approvals, Safe relayer          |
| [Polygon indexing](docs/polygon-indexing.md)         | JSON-RPC watchers, persistent indexer, reorg handling  |
| [CLOB V2 migration](docs/clob-v2-migration.md)       | V2 order signing and exchange contracts                |
| [Benchmarks](docs/benchmarks.md)                     | Benchmark targets and methodology                      |
| [Migration guide](docs/migration.md)                 | Upgrading between major versions                       |
| [Protocol development](docs/protocol-development.md) | Rules for protocol, signing, and stream lifecycle work |
| [Releasing](docs/releasing.md)                       | Release process for maintainers                        |

## Contributing

Contributions are welcome. Start with [CONTRIBUTING.md](CONTRIBUTING.md) for
coding rules, commit format, and the checks CI runs. Coding agents (Claude Code,
Codex, Cursor, Copilot) start with [AGENTS.md](AGENTS.md).

```bash
./build.sh   # build everything and run the offline test suite
```

Bugs and feature requests go to [Issues](https://github.com/SebastianBoehler/polymarket-cpp-client/issues);
security reports follow [SECURITY.md](SECURITY.md).

## Disclaimer

This is an independent open-source project, not affiliated with or endorsed by
Polymarket. Trading involves risk of loss; test with small sizes and read the
code paths you depend on. You are responsible for complying with Polymarket's
terms and the laws of your jurisdiction.

## License

[MIT](LICENSE)
