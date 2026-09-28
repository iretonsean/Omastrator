#!/usr/bin/env bash
# Fills in omastrator-bin's PKGBUILD from a published release and regenerates its .SRCINFO.
# Usage: scripts/update-bin-pkgbuild.sh <version>   (e.g. 0.2.0, matching tag v0.2.0)
set -uo pipefail
cd "$(dirname "$0")/.."
version=${1:-}
pkgbuild=packaging/aur/omastrator-bin/PKGBUILD
if [[ -z $version ]]; then
    echo "Usage: $0 <version>" >&2
    exit 1
fi
if ! command -v gh >/dev/null; then
    echo "gh (github-cli) is required to download the release's checksums." >&2
    exit 1
fi

sums=$(mktemp)
trap 'rm -f "$sums"' EXIT
if ! gh release download "v$version" --repo iretonsean/Omastrator --pattern SHA256SUMS.txt --output "$sums" --clobber; then
    echo "Could not download SHA256SUMS.txt for v$version. Has the release finished publishing?" >&2
    exit 1
fi

x86_64_sum=$(awk -v f="omastrator-$version-x86_64.tar.zst" '$2 == f { print $1 }' "$sums")
aarch64_sum=$(awk -v f="omastrator-$version-aarch64.tar.zst" '$2 == f { print $1 }' "$sums")
if [[ -z $x86_64_sum || -z $aarch64_sum ]]; then
    echo "SHA256SUMS.txt didn't list both tarballs for version $version." >&2
    cat "$sums" >&2
    exit 1
fi

sed -i \
    -e "s/^pkgver=.*/pkgver=$version/" \
    -e "s/^sha256sums_x86_64=.*/sha256sums_x86_64=('$x86_64_sum')/" \
    -e "s/^sha256sums_aarch64=.*/sha256sums_aarch64=('$aarch64_sum')/" \
    "$pkgbuild"

(cd packaging/aur/omastrator-bin && makepkg --printsrcinfo > .SRCINFO)
echo "Updated $pkgbuild and its .SRCINFO for $version."
