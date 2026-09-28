#!/usr/bin/env bash
# Waits (up to 15 minutes) for design mode to turn on, then records 90 s of per-second CPU for the
# busiest processes and design mode's tool, for tracking down sluggishness in Inspect.
# Usage: scripts/profile-design-mode.sh [out-file]   (default: $XDG_RUNTIME_DIR/omastrator-design-profile.txt)
set -uo pipefail
out=${1:-${XDG_RUNTIME_DIR:-/tmp}/omastrator-design-profile.txt}
state() { omastrator status 2>/dev/null | python3 -c 'import json,sys;d=json.load(sys.stdin).get("design",{});print("on" if d.get("on") else "off",d.get("tool"))' 2>/dev/null; }
for _ in $(seq 1 900); do
    [[ $(state) == on* ]] && break
    sleep 1
done
: >"$out"
(while true; do echo "== $(date +%T) $(state)" >>"$out.state"; sleep 1; done) &
loop=$!
top -b -d 1 -n 90 -o %CPU -w 200 | awk '/^top -/{print "== "$3; n=0; next} /^ *[0-9]+ /{if (n++ < 6) print}' >>"$out"
kill "$loop"
echo "recorded to $out (and $out.state)"
