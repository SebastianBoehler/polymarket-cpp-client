#!/usr/bin/env bash
# Configure, build, and run the offline test suite in one step.
#
#   ./build.sh                  Release build in ./build with examples and tests
#   BUILD_TYPE=Debug ./build.sh
#   BUILD_DIR=build-asan CMAKE_ARGS="-DCMAKE_CXX_FLAGS=-fsanitize=address" ./build.sh
#   BENCHMARKS=ON ./build.sh    also build the benchmark targets
#   SKIP_TESTS=1 ./build.sh     build only
set -euo pipefail

cd "$(dirname "$0")"

build_dir="${BUILD_DIR:-build}"
build_type="${BUILD_TYPE:-Release}"
jobs="${JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)}"

extra_args=()
if [[ "$(uname)" == "Darwin" ]]; then
    # Gives clang-tidy an explicit SDK in the compile database.
    sdk_path="$(xcrun --sdk macosx --show-sdk-path 2>/dev/null || true)"
    if [[ -n "${sdk_path}" ]]; then
        extra_args+=("-DCMAKE_OSX_SYSROOT=${sdk_path}")
    fi
fi
if [[ -n "${CMAKE_ARGS:-}" ]]; then
    read -r -a user_args <<<"${CMAKE_ARGS}"
    extra_args+=("${user_args[@]}")
fi

echo "==> Configuring ${build_dir} (${build_type})"
cmake -S . -B "${build_dir}" \
    -DCMAKE_BUILD_TYPE="${build_type}" \
    -DPOLYMARKET_CLIENT_BUILD_EXAMPLES=ON \
    -DPOLYMARKET_CLIENT_BUILD_TESTS=ON \
    -DPOLYMARKET_CLIENT_BUILD_BENCHMARKS="${BENCHMARKS:-OFF}" \
    ${extra_args[@]+"${extra_args[@]}"}

echo "==> Building with ${jobs} jobs"
cmake --build "${build_dir}" --parallel "${jobs}"

if [[ -z "${SKIP_TESTS:-}" ]]; then
    echo "==> Running offline tests"
    ctest --test-dir "${build_dir}" --output-on-failure --parallel "${jobs}" -LE live
fi

echo "==> Done. Binaries are in ${build_dir}/"
