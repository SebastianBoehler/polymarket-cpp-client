# Security policy

This SDK handles private keys, API secrets, and order signatures. Please report
security issues privately so they can be fixed before details are public.

## Reporting a vulnerability

Use GitHub's private reporting:
[Report a vulnerability](https://github.com/SebastianBoehler/polymarket-cpp-client/security/advisories/new).
Do not open a public issue or pull request for a suspected vulnerability.

Include the affected version or commit, the component (for example signing, L2
authentication headers, transport, or position operations), reproduction steps,
and the impact you expect. Never include real private keys, API secrets, or
funded wallet details; use throwaway values.

You can expect an acknowledgement within a few days. Fixes ship in the next
release, and the advisory credits the reporter unless you prefer otherwise.

## Scope

In scope: key and secret handling, signature construction, authentication
headers, TLS and proxy verification, request routing, and anything that could
leak credentials or produce an order or transaction the caller did not request.

Out of scope: vulnerabilities in Polymarket's services or contracts (report
those to Polymarket) and issues that require an already compromised machine.

## Supported versions

Security fixes target the latest release. Upgrade to the newest version before
reporting.
