# Polymarket Arbitrage Bot - C++ Low-Latency Edition

High-performance C++ implementation for Polymarket arbitrage trading with minimal latency.

## Features

- **Low-latency HTTP client** using libcurl with TCP_NODELAY
- **WebSocket orderbook streaming** using libwebsockets
- **Fast JSON parsing** with simdjson and nlohmann/json
- **Lock-free atomic operations** for orderbook state
- **Multi-threaded architecture** for parallel processing

## Requirements

- CMake 3.16+
- C++20 compatible compiler (GCC 10+, Clang 12+, MSVC 2019+)
- libcurl
- OpenSSL

## Building

```bash
cd cpp
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

## Usage

```bash
# Show help
./polymarket_arb --help

# Fetch neg_risk markets and monitor orderbooks
./polymarket_arb

# Fetch only (no WebSocket subscription)
./polymarket_arb --fetch-only

# Monitor 15-minute crypto markets
./polymarket_arb --15m

# Monitor 4-hour crypto markets
./polymarket_arb --4h

# Monitor 1-hour crypto markets
./polymarket_arb --1h

# Custom settings
./polymarket_arb --neg-risk --max 100 --trigger 0.97
```

## Options

| Option         | Description                                      |
| -------------- | ------------------------------------------------ |
| `--help`       | Show help message                                |
| `--fetch-only` | Only fetch markets, don't subscribe to WebSocket |
| `--15m`        | Fetch 15-minute crypto up/down markets           |
| `--4h`         | Fetch 4-hour crypto up/down markets              |
| `--1h`         | Fetch 1-hour crypto up/down markets              |
| `--neg-risk`   | Fetch neg_risk binary markets (default)          |
| `--max N`      | Maximum number of markets to fetch (default: 50) |
| `--trigger N`  | Trigger threshold for arb (default: 0.98)        |

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                        Main Thread                          │
│  - Parse arguments                                          │
│  - Fetch markets via REST API                               │
│  - Print statistics                                         │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                    WebSocket Thread                         │
│  - Connect to Polymarket CLOB WebSocket                     │
│  - Receive orderbook updates                                │
│  - Parse JSON with simdjson                                 │
│  - Update atomic orderbook state                            │
│  - Detect arbitrage opportunities                           │
└─────────────────────────────────────────────────────────────┘
```

## Performance Optimizations

1. **TCP_NODELAY** - Disable Nagle's algorithm for lower latency
2. **Atomic operations** - Lock-free orderbook updates
3. **simdjson** - SIMD-accelerated JSON parsing
4. **Pre-allocated buffers** - Minimize memory allocations
5. **Compiler optimizations** - `-O3 -march=native`

## API Endpoints Used

- **CLOB REST API**: `https://clob.polymarket.com`
  - `GET /markets` - Fetch all markets
  - `GET /book?token_id=...` - Fetch orderbook

- **CLOB WebSocket**: `wss://ws-subscriptions-clob.polymarket.com/ws/market`
  - Subscribe to orderbook updates

- **Gamma API**: `https://gamma-api.polymarket.com`
  - `GET /events?slug=...` - Fetch crypto up/down markets

## Next Steps (Order Placing)

The order placing functionality requires:

1. Wallet private key for signing
2. API key/secret/passphrase for authentication
3. EIP-712 signature generation

See the TypeScript implementation in `src/` for reference.

## License

MIT
