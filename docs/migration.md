# Migration guide

Rebuild consumers against matching headers and libraries whenever you upgrade.
Release notes for each version live in [`docs/releases/`](releases/).

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
