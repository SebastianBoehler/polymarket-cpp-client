#!/usr/bin/env bash
# Runs the pull-request checks from .github/workflows/build.yml and fuzz.yml on
# this machine, one job at a time. macOS jobs cannot run here; the "clang" job
# builds with Clang as the closest local stand-in.
#
# Usage: scripts/ci_local.sh [--base REF] [--title "PR title"] [--fuzz-seconds N]
#                            [--jobs "release debug sanitizers quality fuzz clang"]
#
# Linux CI runs ubuntu-22.04 with GCC 11. Install it (sudo apt-get install g++-11)
# to catch its stricter checks; otherwise the oldest available GCC is used.
set -uo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
base="origin/main"
title=""
fuzz_seconds=60
jobs="title release quality debug sanitizers fuzz clang"
parallel="${CI_LOCAL_PARALLEL:-2}"

while [[ $# -gt 0 ]]; do
    case "$1" in
    --base) base="$2"; shift 2 ;;
    --title) title="$2"; shift 2 ;;
    --fuzz-seconds) fuzz_seconds="$2"; shift 2 ;;
    --jobs) jobs="$2"; shift 2 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done

out="$root/build/ci"
mkdir -p "$out/logs"
declare -a results=()

pick_gcc() {
    for version in 11 12 13 14; do
        if command -v "g++-$version" >/dev/null; then
            echo "gcc-$version g++-$version"
            return
        fi
    done
    echo "gcc g++"
}
read -r cc cxx < <(pick_gcc)
[[ "$cxx" == "g++-11" ]] || echo "note: CI uses g++-11; building with $cxx" >&2

# A Windows npx reached through WSL cannot run Prettier on Linux paths.
if [[ "$(command -v npx)" == /mnt/* ]]; then
    for node_bin in "$HOME"/.nvm/versions/node/*/bin; do
        [[ -x "$node_bin/npx" ]] && PATH="$node_bin:$PATH"
    done
fi

run_job() {
    local name="$1"
    shift
    local log="$out/logs/$name.log"
    local start=$SECONDS
    echo "==> $name (log: ${log#$root/})"
    # errexit is ignored inside an if condition, so the status is read afterwards.
    ( set -e; cd "$root"; "$@" ) >"$log" 2>&1
    if [[ $? -eq 0 ]]; then
        results+=("PASS $name $((SECONDS - start))s")
    else
        results+=("FAIL $name $((SECONDS - start))s")
        tail -n 25 "$log" | sed 's/^/    /'
    fi
}

configure_and_test() {
    local dir="$1" type="$2"
    shift 2
    cmake -S . -B "$dir" -DCMAKE_BUILD_TYPE="$type" "$@"
    cmake --build "$dir" --parallel "$parallel" --config "$type"
}

job_title() {
    local pattern='^(feat|fix|docs|style|refactor|perf|test|build|ci|chore|revert)(\([a-zA-Z0-9_./-]+\))?!?: .+'
    [[ "$title" =~ $pattern ]] || { echo "PR title does not match type(scope): description: $title"; return 1; }
}

job_release() {
    configure_and_test "$out/release" Release -DCMAKE_C_COMPILER="$cc" -DCMAKE_CXX_COMPILER="$cxx" \
        -DPOLYMARKET_CLIENT_BUILD_EXAMPLES=ON -DPOLYMARKET_CLIENT_BUILD_TESTS=ON \
        -DPOLYMARKET_CLIENT_BUILD_BENCHMARKS=ON
    ctest --test-dir "$out/release" --output-on-failure --build-config Release -LE live
}

job_debug() {
    configure_and_test "$out/debug" Debug -DCMAKE_C_COMPILER="$cc" -DCMAKE_CXX_COMPILER="$cxx" \
        -DPOLYMARKET_CLIENT_BUILD_EXAMPLES=ON -DPOLYMARKET_CLIENT_BUILD_TESTS=ON \
        -DPOLYMARKET_CLIENT_BUILD_BENCHMARKS=ON
    ctest --test-dir "$out/debug" --output-on-failure --build-config Debug -LE live
}

job_quality() {
    [[ -f "$out/release/compile_commands.json" ]] || job_release
    local venv="$root/.venv-quality"
    [[ -x "$venv/bin/python3" ]] || python3 -m venv "$venv"
    "$venv/bin/python3" -m pip install --quiet clang-format==21.1.6 clang-tidy==21.1.6
    PATH="$venv/bin:$PATH" python3 scripts/test_quality.py
    PATH="$venv/bin:$PATH" python3 scripts/quality.py "$(git merge-base "$base" HEAD)" --build-dir "$out/release"
}

job_sanitizers() {
    local flags="-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"
    configure_and_test "$out/sanitizers" Debug -DCMAKE_C_COMPILER="$cc" -DCMAKE_CXX_COMPILER="$cxx" \
        -DPOLYMARKET_CLIENT_BUILD_EXAMPLES=OFF -DPOLYMARKET_CLIENT_BUILD_TESTS=ON \
        "-DCMAKE_C_FLAGS=$flags" "-DCMAKE_CXX_FLAGS=$flags" \
        "-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined" \
        "-DCMAKE_SHARED_LINKER_FLAGS=-fsanitize=address,undefined"
    ASAN_OPTIONS=detect_leaks=1:strict_string_checks=1:detect_stack_use_after_return=1 \
        UBSAN_OPTIONS=print_stacktrace=1 \
        ctest --test-dir "$out/sanitizers" --output-on-failure -LE live -E '^test_package_consumer$'
}

job_fuzz() {
    cmake -S . -B "$out/fuzz" -DCMAKE_C_COMPILER=clang-18 -DCMAKE_CXX_COMPILER=clang++-18 \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo -DPOLYMARKET_CLIENT_BUILD_EXAMPLES=OFF \
        -DPOLYMARKET_CLIENT_BUILD_TESTS=OFF -DPOLYMARKET_CLIENT_BUILD_FUZZERS=ON
    for target in websocket_messages rest_responses; do
        cmake --build "$out/fuzz" --parallel "$parallel" --target "fuzz_$target"
        ctest --test-dir "$out/fuzz" --output-on-failure -R "^fuzz_${target}_corpus$"
        local run="$out/fuzz-run/$target"
        rm -rf "$run" && mkdir -p "$run/corpus" "$run/artifacts"
        cp "tests/fuzz/corpus/$target"/* "$run/corpus/"
        # An empty DEBUGINFOD_URLS keeps symbolizing new coverage off the network.
        DEBUGINFOD_URLS= UBSAN_OPTIONS=print_stacktrace=1 "$out/fuzz/fuzz_$target" \
            -max_total_time="$fuzz_seconds" -max_len=4096 -timeout=10 -rss_limit_mb=2048 \
            -print_final_stats=1 -artifact_prefix="$run/artifacts/" "$run/corpus"
    done
}

job_clang() {
    configure_and_test "$out/clang" Release -DCMAKE_C_COMPILER=clang-18 -DCMAKE_CXX_COMPILER=clang++-18 \
        -DPOLYMARKET_CLIENT_BUILD_EXAMPLES=ON -DPOLYMARKET_CLIENT_BUILD_TESTS=ON \
        -DPOLYMARKET_CLIENT_BUILD_BENCHMARKS=ON
    ctest --test-dir "$out/clang" --output-on-failure --build-config Release -LE live
}

for job in $jobs; do
    case "$job" in
    title) [[ -n "$title" ]] && run_job title job_title ;;
    release | debug | quality | sanitizers | fuzz | clang) run_job "$job" "job_$job" ;;
    *) echo "unknown job: $job" >&2; exit 2 ;;
    esac
done

echo
printf '%s\n' "${results[@]}"
for result in "${results[@]}"; do
    [[ "$result" == PASS* ]] || exit 1
done
