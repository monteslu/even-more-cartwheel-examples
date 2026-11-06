#!/usr/bin/env bash
# The launcher art (art/) from the game's own frames. Takes the frames through the GPU lock (tests/pinball-shot.mjs):
# play frames with the HUD on each table, and key frames without it at 3840x2160, then crops and composites them with
# ImageMagick. The logo is build/native/games/pinball/pinball_logo's ("PINBALL" in the dot display's font).
#   games/pinball/tools/make-art.sh
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
OUT="$ROOT/out/art"; ART="$ROOT/games/pinball/art"
mkdir -p "$OUT" "$ART"
node "$ROOT/tests/pinball-shot.mjs" \
  pinball_auto 1500 "$OUT/play-0.png" gl CW_RENDER_SCALE=1.5 PINBALL_TABLE=0 -- \
  pinball_auto 1500 "$OUT/play-1.png" gl CW_RENDER_SCALE=1.5 PINBALL_TABLE=1 -- \
  pinball_auto 1500 "$OUT/play-2.png" gl CW_RENDER_SCALE=1.5 PINBALL_TABLE=2 -- \
  pinball_bench 900 "$OUT/play-3.png" gl CW_RENDER_SCALE=1.5 PINBALL_TABLE=0 -- \
  pinball_bench 700 "$OUT/key-0.png" gl CW_RENDER_SCALE=3 PINBALL_TABLE=0 PINBALL_NO_HUD=1 -- \
  pinball_bench 700 "$OUT/key-1.png" gl CW_RENDER_SCALE=3 PINBALL_TABLE=1 PINBALL_NO_HUD=1 -- \
  pinball_bench 700 "$OUT/key-2.png" gl CW_RENDER_SCALE=3 PINBALL_TABLE=2 PINBALL_NO_HUD=1
"$ROOT/build/native/games/pinball/pinball_logo" "$ART/logo.png" >/dev/null
for i in 0 1 2 3; do convert "$OUT/play-$i.png" -resize 1920x1080! -quality 88 "$ART/shot-$((i + 1)).jpg"; done
convert "$OUT/key-1.png" -gravity center -crop 1440x2160+0+0 +repage -resize 1200x1800! -quality 90 "$ART/cover.jpg"
convert "$OUT/key-2.png" -resize 1920x1080! -brightness-contrast -18x-5 -quality 88 "$ART/background.jpg"
convert "$OUT/key-0.png" -resize 1920x1080! \( "$ART/logo.png" -resize 1100x \) -gravity north -geometry +0+40 -composite -quality 90 "$ART/title.jpg"
convert "$OUT/key-0.png" -gravity south -crop 1500x1500+0+120 +repage -resize 512x512! "$ART/icon.png"
ls -la "$ART"
