# On-Chain Position Operations

`PositionClient` (`include/position_client.hpp`) splits, merges and redeems
binary market positions on Polygon, matching `split_position`,
`merge_positions`, `merge_multiple_positions` and `redeem_positions` in the
official Python SDK.

| Operation | Effect |
|---|---|
| `split_position(condition_id, amount)` | Lock `amount` pUSD into `amount` YES + `amount` NO shares |
| `merge_positions(condition_id, amount = "max")` | Burn YES+NO pairs back into pUSD; `"max"` merges min(YES, NO) |
| `merge_multiple_positions(requests)` | Merge several conditions in one call |
| `redeem_positions(condition_id)` | Redeem every position held in a closed, resolved market |

Amounts are integer base units as base-10 strings. pUSD and outcome shares
both use 6 decimals, so `"1000000"` is 1 pUSD or 1 share.

## Wallets

```cpp
#include "position_client.hpp"

polymarket::PositionClientConfig config;
config.private_key = std::getenv("PRIVATE_KEY");
config.rpc_url = std::getenv("POLYGON_RPC_ENDPOINT");

// Polymarket account wallet (Gnosis Safe), gasless through the relayer:
config.wallet_type = polymarket::SignatureType::POLY_GNOSIS_SAFE;
config.funder_address = std::getenv("POLYMARKET_PROXY_ADDRESS"); // optional check
config.relayer_api_key = std::getenv("RELAYER_API_KEY");

polymarket::PositionClient client(config);
auto handle = client.redeem_positions("0x6b04...6fbc");
auto outcome = handle.wait(); // throws on revert, relayer failure or timeout
```

| `wallet_type` | Holder | Sent by | Gas |
|---|---|---|---|
| `EOA` (default) | the key's address | signed legacy (EIP-155) transaction via `rpc_url` | POL from the EOA |
| `POLY_GNOSIS_SAFE` | the key's Polymarket Safe | Safe transaction via `relayer-v2.polymarket.com` | paid by the relayer |

The Safe address is derived from the key with CREATE2. `funder_address` is
optional; if it is set and is not that Safe, the constructor throws. The
Polymarket profile API calls this wallet `proxyWallet` even though it is a
Safe, not a `POLY_PROXY` wallet. `POLY_PROXY` and `POLY_1271` deposit wallets
are not supported yet and are rejected.

The relayer needs a relayer API key and its address
(`relayer_api_key_address`, default: the signer address). `rpc_url` is still
required for a Safe: it is used for balance reads and receipts.

## How an operation runs

1. The market is looked up on Gamma (`/markets?condition_ids=...`; redeem adds
   `closed=true`). Gamma omits closed markets otherwise, so split and merge only
   work on open markets.
2. The market's `version` picks the contract:
   - `v1` (CTF) markets use the pUSD collateral adapter, or the neg-risk
     collateral adapter when `negRisk` is true, with the ConditionalTokens ABI
     (`splitPosition`, `mergePositions`, `redeemPositions`, partition `[1, 2]`).
   - `v2` markets use the Protocol V2 router (`split`, `merge`,
     `redeem(bytes31, outcomeIndex, amount)`).
3. Merge and redeem read the wallet's balances with `balanceOfBatch` first.
   Merge rejects amounts above min(YES, NO); redeem rejects a wallet with no
   balance instead of spending gas on an empty redeem.
4. The calls are sent:
   - EOA: chain id check, pending nonce, `eth_gasPrice`, `eth_estimateGas`
     (a revert such as a missing approval fails here, before anything is
     sent), sign, `eth_sendRawTransaction`.
   - Safe: relayer nonce, EIP-712 `SafeTx` digest signed as an `eth_sign`
     message (`v + 4`), `POST /submit`. Rate limits, a busy wallet and stale
     nonces are retried with a fresh nonce and signature.

All contract addresses come from `PolymarketContracts` (`polygon_mainnet()` by
default), which can be overridden field by field.

## Batches and atomicity

`merge_multiple_positions`, and a V2 redeem with both outcomes held, produce
several calls:

- **Safe:** one atomic transaction (a `DELEGATECALL` into Safe MultiSend).
- **EOA:** one transaction per call, each mined before the next is sent. This
  is **not atomic**: if one reverts, `TransactionRevertedError` is thrown,
  earlier merges stay done and later ones are not sent.
  `handle.transaction_hashes()` lists every transaction sent.

## Waiting for results

Each operation returns a `TransactionHandle` as soon as the node or relayer
accepts the transaction.

- `transaction_hash()`: empty for a relayer transaction until it is mined.
- `transaction_id()`: the relayer id (Safe only).
- `wait(timeout = 3 min, poll = 2 s)`: returns a `TransactionOutcome` with the
  receipt, or throws:
  - `TransactionRevertedError`: mined with status 0.
  - `TransactionFailedError`: the relayer reported `STATE_FAILED` or
    `STATE_INVALID`.
  - `TransactionTimeoutError`: no outcome in time; it may still be mined.

Invalid input throws `std::invalid_argument`; RPC, Gamma and relayer failures
throw `std::runtime_error`.

## Approvals

Operations do not check or set approvals. Before the first operation a wallet
needs:

- split: a pUSD `approve` for the operator contract (collateral adapter,
  neg-risk collateral adapter or V2 router);
- merge and redeem: `setApprovalForAll` on the position token
  (ConditionalTokens or the V2 position manager) for the same operator.

Wallets that have traded through polymarket.com may already have some of
these; check `allowance` and `isApprovedForAll` if unsure. For an EOA, a
missing approval shows up as a revert during gas estimation, before anything
is sent. For a Safe it surfaces only after submission, as a failed relayer
transaction.

## Example

```bash
# Dry run: resolve the market and print balances
./build/position_example redeem 0x6b04...6fbc

# Send it
./build/position_example redeem 0x6b04...6fbc --execute
./build/position_example split 0x... 1000000 --execute
./build/position_example merge 0x... max --execute
```

The example reads `PRIVATE_KEY` and `POLYGON_RPC_ENDPOINT`. With
`POLYMARKET_PROXY_ADDRESS` set it uses the Safe and the relayer
(`RELAYER_API_KEY`, optional `RELAYER_API_KEY_ADDRESS`); otherwise it sends
from the EOA.

## Not covered yet

- Lookup by market id or position id, and combo (multi-leg) positions.
- `POLY_PROXY` and deposit-wallet (`POLY_1271`) relayer paths.
- Setting trading approvals (`setup_trading_approvals`).

## Verification

- Calldata, signed transactions, Safe address derivation, SafeTx digests,
  signatures, MultiSend packing and relayer payloads match vectors produced
  by the official Python SDK (`test_evm_abi`, `test_evm_abi_shapes`,
  `test_evm_transaction`, `test_position_calls`, `test_safe_relayer`).
- `test_position_client` and `test_safe_relayer_flows` run EOA and Safe flows.
- These flows run against local fake RPC, Gamma and relayer servers.
- Against Polygon mainnet (read-only): the SafeTx digest equals the Safe's own
  `getTransactionHash`, the signature passes the Safe's `checkSignatures`, and
  a simulated `execTransaction` redeem succeeds.
