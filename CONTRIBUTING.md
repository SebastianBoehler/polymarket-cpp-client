# Contributing

## Coding rules

- Use C++20 and match the existing naming, indentation, and brace style.
- Keep new or substantially changed files near 300 lines or fewer. This is a
  soft limit: split by responsibility, not arbitrary line counts. Explain an
  exception in the PR instead of creating meaningless wrappers.
- Keep public interfaces in `include/` and implementation details in `src/`.
  Reuse existing numeric, transport, signing, and error helpers.
- Keep changes surgical. Avoid unrelated cleanup, dependency upgrades, and
  formatting changes. Add configuration or features only when requested.
- Validate inputs at the boundary. Return or throw useful errors with the
  original cause and relevant transaction or request identifiers.
- Handle failure explicitly. Do not hide it with fallback behavior or fabricated
  production data. Deterministic fixtures and local servers belong in tests.
- Use RAII for resources and state ownership. Make callback lifetime, shutdown,
  locking, and concurrent access explicit when changing asynchronous code.
- Treat public type layout and behavior as compatibility concerns. Explain
  changes that require consumers to rebuild or adapt.

Protocol and lifecycle changes also follow
[docs/protocol-development.md](docs/protocol-development.md).

## Commits and pull requests

Use `type(scope): description` or `type: description`. Write a short imperative
subject and group changes by purpose. Common types are `feat`, `fix`, `docs`,
`refactor`, `test`, `perf`, `build`, `ci`, and `chore`. Use `!` and a
`BREAKING CHANGE:` footer when a change requires a migration.

Examples:

- `fix(position): preserve hashes after partial batch failure`
- `test(user-stream): cover authentication rejection`
- `docs: explain agent workflow and coding rules`
- `chore(release): prepare v2.1.0`

PR titles use the same format and are checked in CI. Describe the concrete
problem and resulting behavior, then give the relevant validation. For protocol
changes, include official documentation links and the SDK commit or fixture
source used. Separate observed live results from deterministic test coverage.

Review all tracked and untracked changes before committing. Stage explicit files
or hunks and keep unrelated work in separate commits when its inclusion is
requested. Keep credentials, local environment files, and build artifacts out
of commits.

## Validation

Configure an isolated build directory for the task:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DPOLYMARKET_CLIENT_BUILD_EXAMPLES=ON \
  -DPOLYMARKET_CLIENT_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure -LE live
```

For a small fix, build the affected target and select the relevant tests with
`ctest --test-dir build --output-on-failure -R '<test-name-pattern>' -LE live`.
Add a focused regression for changed behavior. Check that it exposes the failure
before the fix where practical. Assertions must remain effective in Release
builds; use explicit checks such as `tests/check_support.hpp`, not C `assert`
for new tests.

For public headers, dependency exports, or installation changes, run
`test_package_consumer`. It installs the package, builds an external consumer and
shared plugin, and checks subproject configuration. Match the full CI matrix
before releasing: Linux and macOS, Debug and Release. The workflow files define
the authoritative build options and live-test policy.

A successful fake-server test proves the local request and response contract.
It does not prove a transaction succeeded on mainnet. Report the distinction.

## Done means

The requested behavior is implemented, relevant checks pass, and the diff
contains only the intended work. The PR names compatibility changes, evidence,
and checks that were not run. Report blockers instead of claiming completion.

For releases, follow [docs/releasing.md](docs/releasing.md).
