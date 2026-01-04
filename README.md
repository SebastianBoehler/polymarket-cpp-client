# Polymarket C++ Client

[![build](https://github.com/SebastianBoehler/polymarket-cpp-client/actions/workflows/build.yml/badge.svg)](https://github.com/SebastianBoehler/polymarket-cpp-client/actions/workflows/build.yml)
[![release](https://img.shields.io/github/v/release/SebastianBoehler/polymarket-cpp-client)](https://github.com/SebastianBoehler/polymarket-cpp-client/releases)

Reusable C++20 client for Polymarket: REST, WebSocket streaming, and order signing (EIP-712) with examples and tests.

## Features

- **REST**: market discovery, orderbook/price queries, auth key management.
- **WebSocket**: orderbook streaming via IXWebSocket.
- **Signing**: EIP-712 order signing (secp256k1, keccak).
- **Proxy Support**: HTTP/HTTPS proxy with authentication for geo-restricted access.
- **Neg-Risk Markets**: Automatic exchange selection for neg_risk markets.
- **Examples**: REST (`rest_example`), signing (`sign_example`), WebSocket (`ws_example`).
- **Tests**: small utility test (`test_utils`) plus runnable examples.

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
    GIT_TAG v1.0.0  # or any release tag
)
FetchContent_MakeAvailable(polymarket_client)

# Link to your target
target_link_libraries(your_target PRIVATE polymarket::client)
```

### Option 2: Pre-built Releases

Download pre-built binaries from [Releases](https://github.com/SebastianBoehler/polymarket-cpp-client/releases):

```bash
# macOS
curl -LO https://github.com/SebastianBoehler/polymarket-cpp-client/releases/download/v1.0.0/polymarket-cpp-client-macos-arm64.tar.gz
tar -xzf polymarket-cpp-client-macos-arm64.tar.gz -C /usr/local

# Linux
curl -LO https://github.com/SebastianBoehler/polymarket-cpp-client/releases/download/v1.0.0/polymarket-cpp-client-linux-x64.tar.gz
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

Build them with `POLYMARKET_CLIENT_BUILD_EXAMPLES=ON` and run from `build/`.

## Tests

`test_utils` exercises basic utility helpers. Run via `ctest --test-dir build`.

## Key components

- `include/` headers for client API
- `src/http_client.cpp`: libcurl HTTP client
- `src/websocket_client.cpp`: IXWebSocket wrapper
- `src/order_signer.cpp`: EIP-712 signing (secp256k1, keccak)
- `src/clob_client.cpp`: REST + trading endpoints
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

Or directly on HttpClient:

```cpp
#include "http_client.hpp"

polymarket::HttpClient http;
http.set_base_url("https://clob.polymarket.com");
http.set_proxy("http://user:pass@proxy.example.com:8080");
```

## Low-Latency Trading (Keep TCP/TLS Hot)

For high-frequency trading, minimize latency by keeping TCP/TLS connections warm:

```cpp
#include "clob_client.hpp"

polymarket::ClobClient client("https://clob.polymarket.com", 137,
                               private_key, creds);

// 1. Pre-warm connection after startup (establishes TCP/TLS)
client.warm_connection();

// 2. Start background heartbeat to keep connection alive (every 25s)
client.start_heartbeat(25);

// 3. Now your orders will hit ~25-35ms instead of ~40-60ms
auto response = client.create_and_post_order(params);

// 4. Check connection stats
auto stats = client.get_connection_stats();
std::cout << "Avg latency: " << stats.avg_latency_ms << "ms\n";
std::cout << "Reused connections: " << stats.reused_connections << "\n";

// 5. Stop heartbeat when done
client.stop_heartbeat();
```

**Key optimizations enabled:**

- **Connection reuse**: Single CURL handle with `FORBID_REUSE=0`
- **HTTP/1.1 keep-alive**: `Connection: keep-alive` header
- **TCP keepalive**: Probes every 20s to prevent socket close
- **DNS caching**: 60s TTL (configurable via `set_dns_cache_timeout()`)
- **TCP_NODELAY**: Nagle's algorithm disabled for low latency

**Expected gains**: First request ~40-60ms → subsequent requests ~25-35ms.

## Neg-Risk Markets

The client automatically detects neg_risk markets and uses the appropriate exchange address for order signing:

- **Standard markets**: `0x4bFb41d5B3570DeFd03C39a9A4D8dE6Bd8B8982E`
- **Neg-risk markets**: `0xC5d563A36AE78145C45a50134d48A1215220f80a`

This is handled automatically in `create_order()` - no manual intervention needed.

## GitHub Actions

- **build.yml**: CI build on every push/PR (macOS)
- **release.yml**: Automated releases when you push a version tag

### Creating a Release

```bash
# Update version in CMakeLists.txt, then:
git tag v1.0.0
git push origin v1.0.0
```

This triggers the release workflow which builds for macOS and Linux, then creates a GitHub release with downloadable artifacts.

## License

MIT
