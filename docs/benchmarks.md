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

- `benchmark_v2_signing`: signs a fixed V2 order with deterministic salts.
- `benchmark_order_payload`: serializes a fixed V2 order payload.
- `benchmark_http_fixture`: compares cold-client and warm-client GET requests
  against a local loopback HTTP fixture.
- `benchmark_ws_parse`: parses representative market WebSocket JSON messages.

Interpretation:

- Compare numbers on the same machine, compiler, build type, and git branch.
- Prefer Release builds for performance comparisons.
- CI builds the benchmark targets to catch compatibility regressions, but it
  does not fail on timing thresholds.

Live smoke tests are disabled by default. The build workflow only runs the live
REST smoke step when `POLYMARKET_RUN_LIVE_SMOKE=1` is explicitly set.
