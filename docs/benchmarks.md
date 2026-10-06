# Benchmarks

The benchmark targets are small chrono-based executables. They are intended to
make local performance changes visible, not to enforce timing thresholds in CI.

Build them with tests and examples:

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DPOLYMARKET_CLIENT_BUILD_EXAMPLES=ON \
  -DPOLYMARKET_CLIENT_BUILD_TESTS=ON \
  -DPOLYMARKET_CLIENT_BUILD_BENCHMARKS=ON
cmake --build build --parallel
```

Run individual benchmarks:

```bash
./build/benchmark_v2_signing 1000
./build/benchmark_order_payload 100000
./build/benchmark_http_fixture 200
./build/benchmark_ws_parse 100000
```

Targets:

- `benchmark_v2_signing`: signs a fixed V2 order with a real-length 77-digit
  token ID and deterministic salts.
- `benchmark_order_payload`: serializes a fixed V2 order payload.
- `benchmark_http_fixture`: compares cold-client and warm-client GET requests
  against a local loopback HTTP fixture.
- `benchmark_ws_parse`: parses and applies market WebSocket messages to a
  persistent book, the per-frame work `OrderbookManager` does. It reports one
  line per workload: a 100-level book snapshot, a 2-change `price_change`, and a
  2-level book. Identifiers have real lengths (66-77 characters), so string
  copies are not hidden by small-string storage.

## Reference numbers

Best of 7 runs, Release, Apple M4 Max, Apple clang 21:

| Workload                         | Before  | After   | Change |
| -------------------------------- | ------- | ------- | ------ |
| `ws_parse` 100-level book        | 53.7 µs | 28.7 µs | -47%   |
| `ws_parse` 2-change price_change | 5.8 µs  | 3.4 µs  | -42%   |
| `ws_parse` 2-level book          | 4.1 µs  | 2.4 µs  | -42%   |
| `v2_signing`                     | 27.1 µs | 15.6 µs | -42%   |

"Before" is `main` at `a15fbab`, measured with the same benchmark inputs. The gains come from
parsing each frame once without copying the JSON DOM, reading numbers without
string copies, emitting snapshots already in book order, encoding uint256 values
without per-digit allocations, and hex-encoding with a lookup table.

To compare branches, run each benchmark several times and keep the best result
for each line, for example:

```bash
for i in 1 2 3 4 5 6 7; do ./build/benchmark_ws_parse 200000; done | sort
```

Interpretation:

- Compare numbers on the same machine, compiler, build type, and git branch.
- Prefer Release builds for performance comparisons.
- CI builds the benchmark targets to catch compatibility regressions, but it
  does not fail on timing thresholds.

Live smoke tests are disabled by default. The build workflow only runs the live
REST smoke step when `POLYMARKET_RUN_LIVE_SMOKE=1` is explicitly set.
