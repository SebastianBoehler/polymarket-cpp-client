# Polygon JSON-RPC watchers and indexing

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

## Persistent indexer

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

## Live smoke test

`test_oracle_watcher_historical` is labeled `live` and only performs its
historical RPC smoke check when `POLYMARKET_RUN_LIVE_SMOKE=1` is set. Run it
explicitly with `POLYMARKET_RUN_LIVE_SMOKE=1 ctest --test-dir build -L live`.
It uses these defaults unless overridden:

- `POLYMARKET_POLYGON_RPC_HTTP=https://polygon-bor.publicnode.com`
- `POLYMARKET_UMA_CTF_ADAPTER=0x6a9d222616c90fca5754cd1333cfd9b7fb6a4f74`
- `POLYMARKET_CONDITIONAL_TOKENS=0x4d97dcd97ec945f40cf65f87097ace5ea0476045`

Set these env vars explicitly if you need another RPC or contract set.
