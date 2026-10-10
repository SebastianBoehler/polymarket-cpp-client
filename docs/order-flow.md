# Order-Flow Helpers

Four `ClobClient` methods cover the usual trading loop: preview a fill, place
the order in one call, and wait until its fills settle on-chain. They match
`estimate_market_price`, `place_limit_order`, `place_market_order` and
`wait_for_order_fill_settlement` in the official Python SDK. All four return
`Result<T>` and never throw for API, input or liquidity failures.

| Method                                                                                   | Network                                      | Result                |
| ---------------------------------------------------------------------------------------- | -------------------------------------------- | --------------------- |
| `estimate_market_price(token_id, side, amount, order_type = FOK)`                        | book + tick size                             | `MarketPriceEstimate` |
| `estimate_market_price(book, side, amount, tick_size, order_type = FOK)` (free function) | none                                         | `MarketPriceEstimate` |
| `place_limit_order(PlaceLimitOrderParams)`                                               | metadata + `POST /order`                     | `OrderResponse`       |
| `place_market_order(PlaceMarketOrderParams)`                                             | metadata, book unless bounded, `POST /order` | `OrderResponse`       |
| `wait_for_order_fill_settlement(order, timeout = 30s, poll_interval = 250ms)`            | polls `/data/trades?id=`                     | `OrderSettlement`     |

Book responses from `get_order_book`, `get_order_books` and these helpers also
cache the market's tick size and neg-risk flag, so an order placed after a book
fetch skips both metadata lookups. A caller-supplied `tick_size` is used
without a lookup; it is checked against the market minimum only when that
minimum is already cached, and the server rejects orders on a finer grid.

`create_order`, `create_market_order`, `post_order` and the `create_and_post_*`
methods are unchanged. Use them when you need a `SignedOrder` before posting,
for example to batch it with `post_orders`.

## Estimate a market order

```cpp
auto estimate = client.estimate_market_price(token_id, polymarket::OrderSide::BUY, 250.0);
if (estimate)
    std::cout << "worst " << estimate.value().price << ", average "
              << estimate.value().average_price << ", shares "
              << estimate.value().share_units / 1e6 << "\n";
```

`amount` is collateral to spend for BUY and shares to sell for SELL. The walk
starts at the best price and stops at the first level whose cumulative depth
covers the order that would be signed at that level's price.

| Field                             | Meaning                                                                                                       |
| --------------------------------- | ------------------------------------------------------------------------------------------------------------- |
| `price`                           | Deepest level reached; `create_market_order` signs at exactly this price for the same book                    |
| `average_price`                   | Collateral per share across the simulated fill                                                                |
| `share_units`, `collateral_units` | Simulated fill in 6-decimal base units, rounded against you by under one unit per level                       |
| `levels`                          | Price levels the fill touches                                                                                 |
| `fully_fillable`                  | `false` when the book runs out; FOK then fails with `InsufficientLiquidity`, FAK returns the partial estimate |

The free function takes a book you already hold, such as one from
`OrderbookManager::get_orderbook`, and makes no request. Books may list levels
best-first (stream) or best-last (REST); both give the same estimate.

## Place a limit order

```cpp
polymarket::PlaceLimitOrderParams order;
order.token_id = token_id;
order.price = 0.42;
order.size = 10;
order.side = polymarket::OrderSide::BUY;
order.post_only = true;                 // rest on the book or be rejected
order.expiration = unix_now + 3600;     // GTD; leave unset for GTC

auto placed = client.place_limit_order(order);
```

- No `expiration` posts GTC. An `expiration` posts GTD and must be at least
  180 seconds ahead, to allow for latency and clock skew.
- `post_only` is sent as `postOnly`, so the server rejects the order instead of
  matching it on arrival.
- When `tick_size` is empty, a price that is off the cached tick grid
  refreshes the market metadata once before signing, because Polymarket
  narrows the tick near 0 and 1. A price still off the grid is rejected
  without posting.

## Place a market order

```cpp
polymarket::PlaceMarketOrderParams order;
order.token_id = token_id;
order.side = polymarket::OrderSide::BUY;
order.amount = 25.0;                    // collateral for BUY, shares for SELL
order.worst_price = 0.47;               // optional bound
order.order_type = polymarket::OrderType::FOK; // FAK by default

auto placed = client.place_market_order(order);
```

- With `worst_price`, the order is signed at that price without fetching the
  book. A BUY never pays more than `worst_price` per share and a SELL never
  receives less. The server enforces FAK or FOK. A BUY's signed maker and
  taker amounts encode exactly `worst_price`, so a finer tick cannot lift a
  higher ask.
- Without `worst_price`, the price comes from walking the current book as
  `estimate_market_price` does. A FOK the book cannot fill fails with
  `InsufficientLiquidity` before signing.
- The tick refresh works as for limit orders.

## Wait for settlement

A matched order is not final. Each fill moves through `MATCHED`, `MINED` and
`CONFIRMED`, or ends `FAILED` when its transaction reverts. Wait before
treating the shares or collateral as yours:

```cpp
auto settlement = client.wait_for_order_fill_settlement(placed.value());
if (settlement)
    for (const auto &hash : settlement.value().transaction_hashes)
        std::cout << "settled in " << hash << "\n";
```

- Only the fills listed in `OrderResponse::trade_ids` are covered: those
  matched when the order was posted. Later fills of a remainder resting on the
  book are not.
- An order without fills returns at once with the order's own hashes.
- `transaction_hashes` holds the unique hashes of fills that did not fail.
  `trades` holds every fill's final `Trade`, both in `trade_ids` order.
- The wait blocks the calling thread. It polls with a monotonic deadline,
  never sleeps past it, and polls once more at the deadline. A 429 whose
  Retry-After would end past the deadline is returned as `RateLimit` instead
  of being retried.
- The deadline bounds sleeps, not requests. A trade lookup already in flight
  at the deadline runs until the HTTP client's own timeout.
- `get_trade(id)` and `get_trade_result(id)` read one trade directly. They
  return an empty value while the trade is not visible yet.

### Settle from the user stream

Polling adds up to one `poll_interval` of delay and one request per fill.
With a `UserStream` running, feed a `TradeStatusTracker` from its callbacks
and pass it to the wait instead:

```cpp
polymarket::TradeStatusTracker tracker;
stream.on_trade([&](const polymarket::UserTradeEvent &trade) { tracker.record(trade); });
stream.on_stream_recovered([&] { tracker.mark_recovered(); });

auto settlement = client.wait_for_order_fill_settlement(placed.value(), tracker);
```

- The wait returns as soon as the stream reports every fill CONFIRMED or
  FAILED, with the same `OrderSettlement` as the polling form.
- Fills are looked up through REST only after `mark_recovered()`, every
  `reconcile_interval` (5 seconds by default) as a safety net, and once at
  the deadline.
- The tracker keeps the latest status of its most recent `capacity` trades
  (10000 by default). A late earlier-stage event never replaces CONFIRMED or
  FAILED.
- Keep the tracker alive until the stream is stopped, because the stream's
  callbacks hold a reference to it.

## Errors

| Code                    | When                                                                                                                                |
| ----------------------- | ----------------------------------------------------------------------------------------------------------------------------------- |
| `InvalidArgument`       | Bad input, such as a price outside (0, 1), a resting type for a market order, an early GTD expiration, or a price off the tick grid |
| `InsufficientLiquidity` | FOK the book cannot fill, or an empty book side                                                                                     |
| `HttpTransport`         | Tick size, neg-risk, or book request did not complete (retryable)                                                                   |
| `Parse`                 | Tick size, neg-risk, or book response was malformed                                                                                 |
| `ApiResponse`           | The server rejected the order (the message is its `errorMsg`) or a metadata or book lookup (retryable for 5xx)                      |
| `Timeout`               | Fills still settling at the deadline (retryable; the order is unaffected)                                                           |
| `RateLimit`             | A trade lookup got HTTP 429 and its Retry-After does not fit in the remaining wait (retryable)                                      |
| `TransactionFailed`     | Every fill of the order failed                                                                                                      |

Validation errors are returned before any request is sent. Writes are never
retried; see [Rate limits](../README.md#rate-limits).

## Differences from the official SDKs

- `estimate_market_price` returns the full simulated fill, not only the price,
  and has an offline overload for a book you already hold.
- `place_market_order` takes one `worst_price` instead of `max_price` for BUY
  and `min_price` for SELL.
- `max_spend` (a fee-inclusive spend target) is not supported yet.
- Orders rejected for insufficient allowance are not approved and retried
  automatically. Grant approvals once with `PositionClient::setup_trading_approvals`.
