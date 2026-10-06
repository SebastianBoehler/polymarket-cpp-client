# Working in this repository

Read [CONTRIBUTING.md](CONTRIBUTING.md) before changing code or preparing commits.
It defines coding rules, commit names, validation, and the completion criteria.

## Start here

1. Check the current branch, diff, and untracked files. Preserve other work.
2. State the working assumption and success check for larger or ambiguous tasks.
3. Read the affected public headers under `include/polymarket/`, implementation, and existing tests before editing.
4. Keep the diff within the requested scope and match the surrounding C++ style.
5. Run the narrowest meaningful check, then report the command and actual result.

Concurrent work is common. Continue around unrelated changes without resetting,
removing, or reformatting them. When asked to commit all work, review every tracked
and untracked change and create logical commit groups.

## Repository map

| Area                  | Public headers (`include/polymarket/`)                               | Implementation (`src/`)                                                        |
| --------------------- | -------------------------------------------------------------------- | ------------------------------------------------------------------------------ |
| HTTP transport        | `http_client.hpp`, `network.hpp`                                     | `http_client*.cpp`, `http_global.cpp`, `network_route.cpp`                     |
| WebSocket transport   | `websocket_client.hpp`                                               | `websocket_client*.cpp`, `websocket_resilience.cpp`, `websocket_tunnel.cpp`    |
| CLOB REST and trading | `clob_client.hpp`, `clob_types.hpp`, `geoblock.hpp`                  | `clob_*.cpp`, `order_execution.cpp`, `order_amounts.cpp`, `geoblock.cpp`       |
| Order signing         | `order_signer.hpp`, `decimal_math.hpp`                               | `order_signer*.cpp`, `decimal_math.cpp`                                        |
| Market data streams   | `orderbook.hpp`, `types.hpp`                                         | `orderbook*.cpp`, `websocket_market_data.cpp`, `arb_*.cpp`                     |
| User stream           | `user_stream.hpp`                                                    | `user_stream*.cpp`, `websocket_user_data.cpp`                                  |
| On-chain positions    | `position_client.hpp`, `trading_approvals.hpp`, `evm_*.hpp`          | `position_*.cpp`, `approval_calls.cpp`, `safe_relayer.cpp`, `evm_*.cpp`        |
| Polygon indexing      | `json_rpc_client.hpp`, `evm_event_indexer.hpp`, `oracle_watcher.hpp` | `json_rpc_*.cpp`, `evm_*indexer*.cpp`, `oracle_*.cpp`, `polymarket_events.cpp` |

Tests live in `tests/` (fixtures in `*_fixture.hpp` and `*_test_support.hpp`);
some contract tests live in `src/*_contract_tests.cpp`. Benchmarks live in
`benchmarks/`. `./build.sh` configures, builds, and runs the offline suite.

## Load these references when relevant

- Protocol, signing, transactions, numeric parsing, or stream lifecycle changes:
  read [docs/protocol-development.md](docs/protocol-development.md).
- Position operations or wallet support: also read
  [docs/position-operations.md](docs/position-operations.md).
- Proxies, interface binding, or WebSocket transport changes: read
  [docs/networking.md](docs/networking.md). Routed connections must fail closed.
- Hot-path performance changes: run the matching target in `benchmarks/` before
  and after, as described in [docs/benchmarks.md](docs/benchmarks.md).
- Versions, tags, release packages, or publication: read
  [docs/releasing.md](docs/releasing.md) and `.github/workflows/release.yml`.
- Test registration and example targets: inspect `cmake/PolymarketTests.cmake`
  and `cmake/PolymarketExamples.cmake`.

## Authorization and reporting

A request to investigate or draft permits local preparation. Publishing comments,
pushing commits, merging PRs, and publishing releases require authorization from
the user. Carry forward authorization already given in the conversation.

Live orders, approvals, and funded transactions require explicit authorization.
Use deterministic tests and read-only inspection for routine validation.

Distinguish prepared changes, passing local checks, passing CI, and published
artifacts. Report skipped checks and the precise reason. Keep contributor
messages natural and concise. Do not use em dashes.
