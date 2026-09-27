#!/usr/bin/env bash
# Configure, build and run every test with warnings as errors; prints one summary line.
# Usage: scripts/check.sh [build-dir] [jobs]   (jobs defaults to 2: 15 GB machines OOM above 3)
set -uo pipefail
cd "$(dirname "$0")/.."
dir=${1:-build}; jobs=${2:-2}
cmake -S . -B "$dir" -DCMAKE_BUILD_TYPE=Debug -DOMASTRATOR_WERROR=ON >/dev/null || { echo "CHECK FAIL: configure"; exit 1; }
if ! cmake --build "$dir" -j"$jobs" >"$dir/build.log" 2>&1; then
    grep -E "error" "$dir/build.log" | head -20; echo "CHECK FAIL: build (see $dir/build.log)"; exit 1
fi
ctest --test-dir "$dir" -j"$jobs" --timeout 300 >"$dir/ctest.log" 2>&1
summary=$(grep -E "tests passed" "$dir/ctest.log")
if grep -qE "tests failed" "$dir/ctest.log"; then
    sed -n '/The following tests FAILED/,$p' "$dir/ctest.log" | head -20; echo "CHECK FAIL: $summary"; exit 1
fi
echo "CHECK OK: $summary"
