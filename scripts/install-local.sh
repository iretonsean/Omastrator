#!/usr/bin/env bash
# Release build (no tests, LTO, stripped) installed to ~/.local; with --shell, also sync the shell plugins and restart
# omarchy-shell once, then print any Omastrator errors from its log.
set -uo pipefail
cd "$(dirname "$0")/.."
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$HOME/.local" \
    -DOMASTRATOR_BUILD_TESTS=OFF -DOMASTRATOR_LTO=ON >/dev/null &&
    cmake --build build-release -j2 >build-release/build.log 2>&1 && cmake --install build-release --strip >/dev/null ||
    { tail -20 build-release/build.log; echo "INSTALL FAIL"; exit 1; }
echo "installed $(date +%H:%M)"
if [[ ${1:-} == --shell ]]; then
    "$HOME/.local/bin/omastrator" setup --yes | tail -1
    omarchy restart shell >/dev/null 2>&1
    timeout 20 bash -c 'until quickshell list --all 2>/dev/null | grep -q Instance; do :; done'
    id=$(quickshell list --all 2>/dev/null | grep -oE "Instance [a-z0-9]+" | tail -1 | awk '{print $2}')
    errors=$(timeout 8 quickshell log -i "$id" 2>/dev/null | grep -aiE "omastrator|island|overlay" | grep -aiE "error|fail")
    [[ -z $errors ]] && echo "SHELL OK ($id)" || { echo "$errors" | head; echo "SHELL ERRORS"; }
fi
