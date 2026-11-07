#!/usr/bin/env bash
# Frames of one run at several frame counts (the run is deterministic, so they are one motion), side by side in a
# contact sheet, for judging a collapse in motion: games/destructo/tools/motion.sh <app> <sheet.png> <frame...>
# Extra settings come from the environment's MOTION_ENV ("KEY=VALUE ..."). Frames go to out/motion/ (cleared first).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
APP="$1"; SHEET="$2"; shift 2
OUT="$ROOT/out/motion"
mkdir -p "$OUT"
find "$OUT" -maxdepth 1 -name 'f*.png' -delete
args=()
for f in "$@"; do
  [ ${#args[@]} -gt 0 ] && args+=(--)
  args+=("$APP" "$f" "$OUT/f$(printf %05d "$f").png" "${GPU:-gl}" CW_RENDER_SCALE="${SCALE:-0.75}" ${MOTION_ENV:-})
done
node "$ROOT/tests/destructo-shot.mjs" "${args[@]}" | grep -E "status [^0]" || true
montage "$OUT"/f*.png -tile 4x -geometry +4+4 -background '#222' "$SHEET"
echo "$SHEET"
