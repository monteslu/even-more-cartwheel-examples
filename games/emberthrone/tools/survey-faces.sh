#!/usr/bin/env bash
# survey-faces.sh: the program states the speaking faces add (their Face meshes are morphed): emberthrone_auto at every
# graphics preset where people speak (the first shrine with its keeper and the guide, the wardens' rings with the story
# scenes let play), the states each run lacks merged into programs/emberthrone.txt and the C regenerated (its closure
# adds each surveyed face material in every environment its class was surveyed in). On the GPU of the lane it runs on.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
L="$ROOT/out/proglogs-faces"
rm -rf "$L"; mkdir -p "$L/all"
for q in 0 1 2 3 4; do for f in 0 13 22 76; do
  mkdir -p "$L/q${q}f$f"
  CW_PROGRAMS_LOG_DIR="$L/q${q}f$f" CW_RENDER_SCALE=0.25 ADV_SHOT_GPU=1 timeout 300 "$ROOT/games/emberthrone/tools/swshot.sh" emberthrone_auto 360 /dev/null \
    ADV_QUALITY=$q ADV_FROM=$f ADV_STORY=1 >/dev/null 2>&1 || true
done; done
{ cat "$L"/q*/emberthrone_auto.log 2>/dev/null || true; } | sort -u > "$L/all/emberthrone_auto.log"
echo "$(wc -l < "$L/all/emberthrone_auto.log") missing states reported"
[ -s "$L/all/emberthrone_auto.log" ] || exit 0
CW_PROJECT_ROOT="$ROOT" node "$ROOT/deps/cartwheel-engine/tools/programs.mjs" emberthrone_auto --no-run --build "$ROOT/build/native" --logs "$L/all"
