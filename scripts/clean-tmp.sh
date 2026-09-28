#!/usr/bin/env bash
# Removes this user's stale Qt Test temporary directories from /tmp (RAM-backed here). Qt
# Test names them <TestBinary>-XXXXXX (LiveReviewTests-* and friends); QTemporaryDir removes
# its own on a clean run, but a crash, a timeout kill or an out-of-memory kill skips that, and
# LiveReviewTests' Chromium profile alone can be well over 100 MB. Run before a big build.
set -uo pipefail
find /tmp -mindepth 1 -maxdepth 1 -name '*Tests-??????' -user "$(id -un)" -mmin +60 -print -exec rm -rf {} + 2>/dev/null
true
