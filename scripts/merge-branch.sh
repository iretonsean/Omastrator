#!/usr/bin/env bash
# Merge a finished branch into main, check, sweep and push. Stops at the first problem
# (conflicts are left in place for a person or agent to resolve). Usage: scripts/merge-branch.sh <branch>
set -uo pipefail
cd "$(dirname "$0")/.."
branch=${1:?branch}
[[ $(git rev-parse --abbrev-ref HEAD) == main ]] || { echo "MERGE FAIL: not on main"; exit 1; }
[[ -z $(git status --porcelain --untracked-files=no) ]] || { echo "MERGE FAIL: main has uncommitted changes"; exit 1; }
git pull -q --rebase origin main || { echo "MERGE FAIL: pull"; exit 1; }
if ! git merge --no-edit -q "$branch"; then
    git diff --name-only --diff-filter=U; echo "MERGE CONFLICT: resolve, commit, then run scripts/check.sh and scripts/sweep.sh"; exit 2
fi
scripts/check.sh || exit 1
scripts/sweep.sh || exit 1
git push -q origin main && echo "MERGE OK: $(git log --oneline -1)"
