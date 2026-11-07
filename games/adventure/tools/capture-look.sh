#!/usr/bin/env bash
# capture-look.sh: one play frame from each region (no HUD, 1920x1080, software GL) into out/look/r<region>.png, for
# tests/adventure-look.mjs
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
O="$ROOT/out/look"; mkdir -p "$O"
S="$ROOT/games/adventure/tools/swshot.sh"
printf '%s\n' "meadow 1 240" "canyon 6 300" "marsh 20 300" "keep 180 240" "heights 39 240" "underway 55 320" "spire 61 240" "wood 28 240" |
  xargs -P 4 -L 1 bash -c 'n=$0 f=$1 fr=$2; ADV_NO_HUD=1 ADV_FROM=$f "'"$S"'" adventure_auto $fr "'"$O"'/r$n.png" >/dev/null 2>&1 || echo "capture $n failed"'
ls "$O"
