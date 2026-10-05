# Protocol and lifecycle changes

## Verify the contract

Cross-check behavior against both the current
[official documentation](https://docs.polymarket.com/) and the
[official Python SDK](https://github.com/Polymarket/py-sdk).
Record exact documentation pages and the SDK commit used in the PR. Inspect
wire payloads, contract signatures, and SDK tests, not just method names.
When sources disagree, resolve the discrepancy before implementing it and
explain the evidence. An SDK fixture is evidence for that encoded case, not
proof that a public API accepts all required inputs.

Use `PolymarketContracts` for shared contract addresses. Verify the chain,
collateral token, wallet type, operator, and approvals for the affected path.
Keep CLOB V2 order signing distinct from Protocol V2 position operations.

## Preserve numeric and identifier meaning

Use scaled integers or validated integer strings for money and on-chain
amounts. Reuse the decimal and EVM integer helpers; check width and overflow.
Do not narrow uint256 balances to machine integers or use floating point for
transaction amounts.

Validate identifiers where their contract is known. A CTF condition is bytes32;
a Protocol V2 market lookup can accept bytes31. Preserve the caller's valid
lookup ID until Gamma resolves the market, then validate the selected ABI.
Test these through public methods as well as internal encoding helpers.

## Transactions and retries

EOA batches send separate transactions. If a later call fails, earlier calls
remain submitted. Preserve their hashes, the failed call index, and the original
exception through `PartialBatchError`. Test failures before the first submission,
after partial progress, and after a mined revert.

A Safe MultiSend batch is atomic on-chain. Keep submission acceptance, relayer
state, and a mined receipt distinct. A timeout does not establish failure or
revert. Retry only failures whose semantics permit retry; reconcile uncertain
submission outcomes before sending another transaction.

Use monotonic deadlines for waits. Bound sleep by the remaining time and retain
a final outcome check. Cover `timeout < poll interval` and completion near the
deadline for both receipt and relayer paths. Account for transport request time
when describing elapsed-time guarantees.

## Streams and callbacks

Treat missed events as stale state. Report gaps on disconnect, queue overflow,
and invalid payloads. Trigger recovery after subscriptions have been restored
so callers can reconcile through REST. Reconnect alone cannot replay missed
user events.

Test terminal authentication rejection, late connection completion after a
timeout, subscription replay, recovery ordering, and shutdown from callbacks
when changing the corresponding behavior. Ensure stopped sessions cannot start
delivering events again through an old transport callback.

Keep local fixture coverage, read-only RPC checks, and funded transaction tests
separate in reports. Use explicit authorization before any operation that moves
funds or changes approvals.
