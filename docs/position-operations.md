# On-Chain Position Operations

`PositionClient` (`include/polymarket/position_client.hpp`) splits, merges and redeems
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

`condition_id` is `0x` plus 32 bytes of hex. A Protocol V2 market may also be
given by its 31-byte (bytes31) id; it is passed to Gamma as given.

## Wallets

```cpp
#include "polymarket/position_client.hpp"

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
  is **not atomic**: earlier merges stay done and later ones are not sent.
  `handle.transaction_hashes()` lists every transaction sent. If a call fails
  after an earlier one reached the node (a revert, a failed gas estimate, an
  RPC error), `PartialBatchError` is thrown: `submitted_hashes()` lists the
  transactions already sent, `failed_call_index()` the call that failed, and
  `cause()` holds the original exception (for example
  `TransactionRevertedError`).

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

  Polling never sleeps past the deadline, and the last poll happens at the
  deadline. So a timeout shorter than `poll` still waits the full timeout:
  `wait(1s, 2s)` polls at 0 s and at 1 s before giving up.

Invalid input throws `std::invalid_argument`; RPC, Gamma and relayer failures
throw `std::runtime_error`. An EOA batch that fails after sending part of its
calls throws `PartialBatchError` (see [Batches and atomicity](#batches-and-atomicity)).

## Approvals

Operations do not check approvals. Before the first operation a wallet needs:

- split: a pUSD `approve` for the operator contract (collateral adapter,
  neg-risk collateral adapter or V2 router);
- merge and redeem: `setApprovalForAll` on the position token
  (ConditionalTokens or the V2 position manager) for the same operator.

For an EOA, a missing approval shows up as a revert during gas estimation,
before anything is sent. For a Safe it surfaces only after submission, as a
failed relayer transaction.

### Trading approvals

`setup_trading_approvals()` grants, once per wallet, the same set as the
official SDKs' `setup_trading_approvals` (`required_trading_approvals(contracts)`
in `include/polymarket/trading_approvals.hpp`): 17 approvals that cover CLOB
trading as well as split, merge and redeem.

| Token                   | Approval                            | Spenders / operators                                                                                            |
| ----------------------- | ----------------------------------- | --------------------------------------------------------------------------------------------------------------- |
| pUSD `collateral_token` | `approve(spender, 2^256 - 1)`       | standard and neg-risk exchanges, both collateral adapters, V2 router, exchange V3, perps deposit contract       |
| ConditionalTokens       | `setApprovalForAll(operator, true)` | standard and neg-risk exchanges, both collateral adapters, auto-redeem operator, binary module, neg-risk module |
| V2 `position_manager`   | `setApprovalForAll(operator, true)` | V2 router, exchange V3, auto-redeem operator                                                                    |

```cpp
auto state = client.get_trading_approvals_state(); // or (other_wallet)
if (!state.is_fully_approved)
    client.setup_trading_approvals(); // sends only state.missing, then waits
```

- `get_trading_approvals_state(wallet = this wallet)` reads every approval
  with `eth_call` (`allowance`, `isApprovedForAll`) and lists only the missing
  ones. An allowance below `2^256 - 1` counts as missing, as in the official
  SDKs. The official SDKs read this from the Data API (`/v2/approvals`), which
  can lag; this client always reads the chain.
- `setup_trading_approvals(timeout = 3 min)` sends `approve` calls first, then
  `setApprovalForAll`, and waits. A Safe sends them as one atomic MultiSend
  transaction; an EOA sends one transaction per approval (not atomic, see
  [Batches and atomicity](#batches-and-atomicity); rerunning picks up where it
  stopped). It returns `std::nullopt` without sending anything when the wallet
  is already fully approved.

Single approvals and transfers return a `TransactionHandle` without waiting:

| Method                                                      | Call                                                                            |
| ----------------------------------------------------------- | ------------------------------------------------------------------------------- |
| `approve_erc20(token, spender, amount)`                     | ERC-20 `approve`; `amount` is a uint256 in base units or `"max"`, `"0"` revokes |
| `approve_erc1155_for_all(token, operator, approved = true)` | ERC-1155 `setApprovalForAll`                                                    |
| `transfer_erc20(token, recipient, amount)`                  | ERC-20 `transfer` of a positive base-unit amount                                |

Each also takes an optional relayer `metadata` string.

## Example

```bash
# Trading approvals: list the missing ones, then grant them
./build/approvals_example
./build/approvals_example --execute

# Dry run: resolve the market and print balances
./build/position_example redeem 0x6b04...6fbc

# Send it
./build/position_example redeem 0x6b04...6fbc --execute
./build/position_example split 0x... 1000000 --execute
./build/position_example merge 0x... max --execute

# Protocol V2 markets also accept the 31-byte condition id
./build/position_example split 0x01...44 1000000 --execute
```

The examples read `PRIVATE_KEY` and `POLYGON_RPC_ENDPOINT`. With
`POLYMARKET_PROXY_ADDRESS` set it uses the Safe and the relayer
(`RELAYER_API_KEY`, optional `RELAYER_API_KEY_ADDRESS`); otherwise it sends
from the EOA.

## Not covered yet

- Lookup by market id or position id, and combo (multi-leg) positions.
- `POLY_PROXY` and deposit-wallet (`POLY_1271`) relayer paths.
- Reading approvals from the Data API (`/v2/approvals`); state is read on chain.

## Verification

- Calldata, signed transactions, Safe address derivation, SafeTx digests,
  signatures, MultiSend packing and relayer payloads match vectors produced
  by the official Python SDK (`test_evm_abi`, `test_evm_abi_shapes`,
  `test_evm_transaction`, `test_position_calls`, `test_safe_relayer`).
- `test_position_client` and `test_safe_relayer_flows` run EOA and Safe flows;
  `test_trading_approvals` runs approval reads, setup (EOA and Safe MultiSend),
  single approvals and transfers; `test_approval_calls` checks approval and
  transfer calldata against the py-sdk golden vectors;
  `test_transaction_waiters` covers receipt and relayer waits whose timeout is
  shorter than the poll interval.
- These flows run against local fake RPC, Gamma and relayer servers.
- Against Polygon mainnet (read-only): the SafeTx digest equals the Safe's own
  `getTransactionHash`, the signature passes the Safe's `checkSignatures`, and
  a simulated `execTransaction` redeem succeeds.
- Against Polygon mainnet with real funds:
  - A Safe split and merge of 1 base unit went through the relayer.
    `wait(1s, 2s)` on the pending split timed out after about 1.3 s.
  - An EOA batch whose second call fails gas estimation threw
    `PartialBatchError` with the mined hash of the first call.
  - `setup_trading_approvals` on a Safe that lacked only the perps deposit
    allowance sent that one `approve` through the relayer; a fresh
    `get_trading_approvals_state` then reported the Safe fully approved.
  - No Protocol V2 market was listed on Gamma yet, so 31-byte ids were only
    checked against fixtures from the official Python SDK.
