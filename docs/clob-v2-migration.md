# CLOB V2 Migration Plan

Polymarket CLOB V2 is not a simple endpoint rename. Production still uses
`https://clob.polymarket.com`, but V2 changed order signing, fee handling,
builder attribution, exchange contracts, and collateral handling.

This branch tracks the migration work separately from smaller V1-compatible
bug fixes.

## What Changed

- Production CLOB is V2 as of April 28, 2026.
- Legacy V1-signed orders are no longer accepted in production.
- L1/L2 API authentication headers are unchanged.
- EIP-712 exchange signing changed from domain version `"1"` to `"2"`.
- Exchange verifying contracts changed for standard and negative-risk markets.
- Signed order fields changed:
  - removed from the signed struct: `taker`, `expiration`, `nonce`, `feeRateBps`
  - added to the signed struct: `timestamp`, `metadata`, `builder`
- The POST `/order` wire body still carries `expiration` for GTD behavior.
- Fees are determined by protocol/market state at match time, not embedded in
  the signed order.
- Builder attribution moved from `POLY_BUILDER_*` HMAC headers into a signed
  `builder` bytes32 field.
- Collateral moved from USDC.e to pUSD for CLOB trading workflows.

## Repository Impact

The current client still has V1-shaped order structs and signer logic:

- `OrderData` and `SignedOrder` include `nonce`, `fee_rate_bps`, and `taker`.
- `OrderSigner::hash_order` signs the V1 EIP-712 order type.
- `create_order`, `post_order`, and batch order serialization emit V1 order
  payloads.
- Example trading flows still describe sizes and balances as USDC.

These areas need deliberate migration because changing them affects every
authenticated trading path.

## Proposed Work

1. Add explicit CLOB protocol versioning in the public API.
2. Introduce V2 order data types instead of overloading the V1 structs.
3. Implement V2 EIP-712 order hashing with the version `"2"` exchange domain.
4. Replace hard-coded V1 exchange addresses with named V1/V2 contract config.
5. Update limit and market order creation to populate `timestamp`, `metadata`,
   and `builder`.
6. Remove user-settable `feeRateBps`, `nonce`, and `taker` from V2 order
   creation params.
7. Add `get_clob_market_info` or equivalent fee/market metadata support.
8. Update POST `/order` and batch serialization for the V2 wire body.
9. Update examples and README language from USDC.e assumptions to pUSD-aware
   trading flows.
10. Add signer-level tests with fixed vectors before enabling V2 order posting
    by default.

## Open Questions

- Should V1 code paths remain available behind an explicit compatibility mode,
  or should this library move directly to V2-only trading?
- Which wallet/signature type names should the C++ API expose for deposit
  wallet flows, especially `POLY_1271`?
- Do we want builder code support in the first V2 implementation or a follow-up
  PR?

## References

- https://docs.polymarket.com/v2-migration
- https://docs.polymarket.com/api-reference/clients-sdks
- https://docs.polymarket.com/trading/clients/l2
