#!/usr/bin/env bash
# Commit any uncommitted work in every agent worktree as WIP and push its branch.
set -uo pipefail
cd "$(dirname "$0")/.."
for w in .claude/worktrees/*; do
    [[ -d $w ]] || continue
    if [[ -n $(git -C "$w" status --porcelain) ]]; then
        git -C "$w" add -A && git -C "$w" commit -qm "WIP: rescued uncommitted agent work" && echo "rescued $w"
    fi
    git -C "$w" push -q -u origin HEAD 2>/dev/null && echo "pushed $(git -C "$w" rev-parse --abbrev-ref HEAD)"
done
