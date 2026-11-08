#!/usr/bin/env bash
# capture-chars.sh <outdir> [name ...]: close-ups of the cast (no HUD, 1920x1080) into <outdir>/<name>-<view>.png: each
# figure at the game's camera distance and at a portrait distance, from the bot's run (ADV_FROM puts it where that
# figure is; ADV_CAM_KIND frames the nearest of that kind). On the GPU the suite lock gives the batch (ADV_SHOT_GPU=1).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
# one GPU suite lock for the batch, its GPU drawing the shots
if [ -z "${CW_GPU_SUITE_INNER:-}" ]; then CW_GPU_SUITE_INNER=1 exec node "$ROOT/deps/cartwheel-engine/tools/with-gpu-suite.mjs" emberthrone-capture-chars bash "${BASH_SOURCE[0]}" "$@"; fi
O="$1"; shift; mkdir -p "$O"
S="$ROOT/games/emberthrone/tools/swshot.sh"
WANT=" $* "
# name kind from frames height(m) players game-view-yaw (0 behind, 3.14 in front)
LIST="wayfarer 0 1 150 1.45 1 0.6
warden 1 1 150 1.5 2 0.6
raider 2 6 400 1.45 1 3.0
brute 3 6 520 1.9 1 3.0
lancer 4 20 330 1.55 1 3.0
thornwarden 5 12 520 2.6 1 3.0
tidecaller 6 22 520 2.4 1 3.0
emberking 7 180 520 3.0 1 3.0"
shot() { # name view dist pitch yaw height kind from frames players
  ADV_SHOT_GPU=1 ADV_NO_HUD=1 ADV_GOD=1 ADV_PLAYERS="${10}" ADV_FROM="$8" ADV_CAM_KIND="$7" ADV_CAM_DIST="$3" ADV_CAM_PITCH="$4" ADV_CAM_YAW="$5" ADV_CAM_H="$6" \
    "$S" emberthrone_auto "$9" "$O/$1-$2.png" >/dev/null 2>&1 || echo "capture $1 $2 failed"
}
while read -r n k f fr h p gy; do
  [ "$WANT" = "  " ] || [[ "$WANT" == *" $n "* ]] || continue
  sc=$(node -e "console.log(($h/1.5).toFixed(3))")
  shot "$n" game "$(node -e "console.log((5.6*$sc).toFixed(2))")" -0.26 "$gy" "$(node -e "console.log((1.0*$sc).toFixed(2))")" "$k" "$f" "$fr" "$p" &
  shot "$n" portrait "$(node -e "console.log((1.25*$sc).toFixed(2))")" -0.06 3.3 "$h" "$k" "$f" "$fr" "$p" &
  shot "$n" body "$(node -e "console.log((3.2*$sc).toFixed(2))")" -0.12 2.7 "$(node -e "console.log((0.95*$sc).toFixed(2))")" "$k" "$f" "$fr" "$p" &
  wait
done <<< "$LIST"
ls "$O"
