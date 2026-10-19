# Migration guide

Rebuild consumers against matching headers and libraries whenever you upgrade.
Release notes for each version live in [`docs/releases/`](releases/).

## v4

The string-based `ClobClient` constructors now accept only chain 137. Code such
as `ClobClient(url, 80002)` throws `std::invalid_argument`; it previously paired
that chain ID with production exchange addresses. For another deployment, pass
an `Environment` containing its endpoints, chain ID, and contract addresses.
Changing only the URL in a string constructor still changes only the production
CLOB host, not the Data API or contracts.

```cpp
#include <polymarket/clob_client.hpp>
#include <polymarket/environment.hpp>
#include <polymarket/position_client.hpp>

const auto env = polymarket::Environment::production();
polymarket::ClobClient client(env);
const auto stream_config = polymarket::Config::for_environment(env);
const auto position_config = polymarket::PositionClientConfig::for_environment(env);
```

For a fork or local chain, copy a preset and replace all endpoints and contracts
that differ before passing it to a client. `Environment::preproduction()` is
not a testnet: it uses Polygon mainnet and production contracts. Orders and
on-chain operations can use real funds, and API credentials are per environment.

Rebuild all consumers and shared plugins against matching v4 headers and
libraries. Public layouts changed, including `ClobClient`, `MarketFetcher`,
`PolymarketContracts`, `HttpClientOptions`, `WebSocketOptions`, `WebSocketStats`,
`Config`, and `SdkError`. Include paths remain `<polymarket/...>` and the CMake
target remains `polymarket::client`. CMake package version checks reject a v3
requirement against v4; update `find_package(polymarket_client 3 ...)` to request
version 4 if you specify a major version.

Review these behavior changes when upgrading:

- Rate-limited reads now retry HTTP 429 up to twice using `Retry-After`, subject
  to a five-second maximum delay. Writes are not retried. Use
  `set_rate_limit_retry(std::nullopt)` to disable read retries on `ClobClient`.
- Heartbeats no longer contribute to HTTP connection statistics or last-request
  metrics. A caller-supplied `tick_size` is checked against a cached minimum
  when available; otherwise the server validates it.
- Settlement waits require final transaction hashes for confirmed fills. A
  confirmation without hashes triggers REST reconciliation and remains pending
  if the hashes are still missing.
- `SdkErrorCode` adds `InsufficientLiquidity`, `Timeout`, and
  `TransactionFailed`. Update exhaustive switches.

See [the v4.0.0 release notes](releases/v4.0.0.md) for the new data clients,
pagers, streams, and trading helpers.

## v3

Public headers now live under `polymarket/`. Update includes such as
`#include <http_client.hpp>` to `#include <polymarket/http_client.hpp>`.
The CMake target remains `polymarket::client`. Rebuild consumers with the v3
headers and libraries. Flat compatibility headers are not installed. Use a clean install prefix;
installing over v2 does not remove its old flat headers.

## v2

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
