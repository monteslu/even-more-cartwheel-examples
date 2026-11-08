#!/usr/bin/env bash
# Lay out the frames strip.mjs captured (out/strips/<set>/<name>.png.d) as strips, where ImageMagick is:
#   games/skate/tools/assemble.sh <set>
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
for d in "$ROOT/out/strips/$1"/*.png.d; do
  crop=$(cat "$d/crop.txt" 2>/dev/null || echo 360x480)
  args=()
  for f in "$d"/t*.png; do args+=( "(" "$f" -gravity center -crop "${crop}+0+0" +repage ")" ); done
  convert "${args[@]}" +append -resize '50%' "${d%.d}"
done
ls "$ROOT/out/strips/$1"/*.png
