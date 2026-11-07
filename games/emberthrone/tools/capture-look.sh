#!/usr/bin/env bash
# capture-look.sh: one play frame from each region (no HUD, 1920x1080, software GL) into out/look/r<region>.png, for
# tests/emberthrone-look.mjs
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
# one GPU suite lock for the whole batch (software GL, but it loads the machine: the quality timing runs need it quiet), two shots at a time
if [ -z "${CW_GPU_SUITE_INNER:-}" ]; then CW_GPU_SUITE_INNER=1 exec node "$ROOT/deps/cartwheel-engine/tools/with-gpu-suite.mjs" emberthrone-capture-look bash "${BASH_SOURCE[0]}" "$@"; fi
O="$ROOT/out/look"; mkdir -p "$O"
S="$ROOT/games/emberthrone/tools/swshot.sh"
printf '%s\n' "meadow 1 240" "canyon 6 300" "marsh 20 300" "keep 180 240" "heights 39 240" "underway 55 320" "spire 61 240" "wood 28 240" |
  xargs -P 2 -L 1 bash -c 'n=$0 f=$1 fr=$2; ADV_NO_HUD=1 ADV_FROM=$f "'"$S"'" emberthrone_auto $fr "'"$O"'/r$n.png" >/dev/null 2>&1 || echo "capture $n failed"'
ls "$O"
