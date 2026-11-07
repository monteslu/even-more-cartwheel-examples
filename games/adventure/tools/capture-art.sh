#!/usr/bin/env bash
# capture-art.sh: the frames tools/make-art.mjs composes the launcher art from (out/art/<name>.png, 1920x1080, no HUD),
# each from adventure_auto at a point of the route (ADV_FROM) after a number of frames, on software GL
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
O="$ROOT/out/art"; mkdir -p "$O"
S="$ROOT/games/adventure/tools/swshot.sh"
shot() { local name="$1" from="$2" frames="$3"; shift 3; ADV_NO_HUD=1 ADV_FROM="$from" "$S" adventure_auto "$frames" "$O/$name.png" "$@" >/dev/null 2>&1 || echo "capture $name failed"; }
shot meadow 1 260 &
shot fight 6 520 &
shot marsh 20 330 &
shot keep 180 260 &
wait
shot boss 22 700 &
shot vista 2 200 ADV_CAM_DIST=16 ADV_CAM_PITCH=-0.32 &
shot hero 1 200 ADV_CAM_DIST=2.6 ADV_CAM_PITCH=-0.05 ADV_CAM_YAW=3.4 &
wait
ls "$O"
