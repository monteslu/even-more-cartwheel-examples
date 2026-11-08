#!/usr/bin/env bash
# capture-look.sh [out dir] [app]: five play frames from each region spread over its fight (frames 240, 300, 360, 420
# and 480 from the route point; no HUD, 1920x1080, software GL) into out/look/r<region>-<frame>.png, for
# tests/emberthrone-look.mjs, which judges each region on the median of its frames (one frame is one moment of a
# fight: where the camera happens to point moves its numbers more than the light does). The app defaults to this
# tree's emberthrone_auto; another build's binary (its .cwpak beside it) measures that build the same way.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
# one GPU suite lock for the whole batch (software GL, but it loads the machine: the quality timing runs need it quiet), two shots at a time
if [ -z "${CW_GPU_SUITE_INNER:-}" ]; then CW_GPU_SUITE_INNER=1 exec node "$ROOT/deps/cartwheel-engine/tools/with-gpu-suite.mjs" emberthrone-capture-look bash "${BASH_SOURCE[0]}" "$@"; fi
O="${1:-$ROOT/out/look}"; mkdir -p "$O"
APP="${2:-$ROOT/build/native/games/emberthrone/emberthrone_auto}"
export O APP
for r in "meadow 1" "canyon 6" "marsh 20" "keep 180" "heights 39" "underway 55" "spire 61" "wood 28"; do
  [ -n "${LOOK_REGIONS:-}" ] && [[ " $LOOK_REGIONS " != *" ${r%% *} "* ]] && continue # LOOK_REGIONS="canyon marsh": only those
  for fr in 240 300 360 420 480; do echo "$r $fr"; done
done | xargs -P "${LOOK_JOBS:-2}" -L 1 bash -c 'n=$0 f=$1 fr=$2; env LIBGL_ALWAYS_SOFTWARE=1 LP_NUM_THREADS=4 CW_WORKERS=3 SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
  CW_GPU=gl CW_FRAMES=$fr CW_WARMUP_SPREAD=0 CW_SCREENSHOT="$O/r$n-$fr.png" ADV_NO_HUD=1 ADV_FROM=$f nice -n 5 "$APP" >/dev/null 2>&1 || echo "capture $n $fr failed"'
ls "$O" | wc -l
