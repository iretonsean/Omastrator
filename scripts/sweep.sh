#!/usr/bin/env bash
# Personal-data sweep of what a push would add (public repo). Usage: scripts/sweep.sh [base-ref]
# Looks for this machine's user, home path, git email and host, plus token and key shapes.
set -uo pipefail
cd "$(dirname "$0")/.."
base=${1:-origin/main}
user=$(id -un); host=$(uname -n); mail=$(git config --global user.email 2>/dev/null)
pattern="/home/$user|$HOME|gh[pousr]_[A-Za-z0-9]{20,}|sk-ant-[A-Za-z0-9-]{10,}|AKIA[0-9A-Z]{16}|BEGIN (RSA|OPENSSH|EC) PRIVATE KEY"
[[ -n $user ]] && pattern="$pattern|\\b$user\\b"
# A stock hostname (archlinux, localhost, omarchy) names nothing personal and matches the Arch images, AUR and Omarchy itself.
[[ -n $host && ! $host =~ ^(archlinux|localhost|arch|omarchy)$ ]] && pattern="$pattern|\\b$host\\b"
[[ -n $mail && $mail != *noreply* ]] && pattern="$pattern|$mail"
hits=$(git diff "$base"...HEAD -- . ':!third_party' | grep -E '^\+' | grep -nE "$pattern" | head -20)
authors=$(git log --format='%ae' "$base"..HEAD | sort -u | grep -v noreply)
if [[ -n $hits || -n $authors ]]; then
    echo "$hits"; [[ -n $authors ]] && echo "non-noreply authors: $authors"; echo "SWEEP FAIL"; exit 1
fi
echo "SWEEP OK"
