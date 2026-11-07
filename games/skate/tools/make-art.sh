#!/usr/bin/env bash
# The launcher art (art/) from the game's own frames. Takes the frames through the GPU lock (tests/skate-shot.mjs):
# play frames with the HUD (two players in split screen on four parks), and key frames without it at 3840x2160 (one
# skater), then crops and composites them with ImageMagick. The logo is the working title set in the engine's sans.
#   games/skate/tools/make-art.sh
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
OUT="$ROOT/out/art"; ART="$ROOT/games/skate/art"
FONT="$ROOT/deps/cartwheel-engine/src/ui/fonts/FiraSans-Regular.ttf"
mkdir -p "$OUT" "$ART"
node "$ROOT/tests/skate-shot.mjs" \
  skate_auto 727 "$OUT/play-0.png" gl CW_RENDER_SCALE=1.5 SKATE_PARK=0 -- \
  skate_auto 742 "$OUT/play-1.png" gl CW_RENDER_SCALE=1.5 SKATE_PARK=2 -- \
  skate_auto 662 "$OUT/play-2.png" gl CW_RENDER_SCALE=1.5 SKATE_PARK=1 -- \
  skate_auto 800 "$OUT/play-3.png" gl CW_RENDER_SCALE=1.5 SKATE_PARK=3 -- \
  skate_auto 900 "$OUT/readme.png" gl CW_RENDER_SCALE=1.5 SKATE_PARK=4 SKATE_PLAYERS=4 -- \
  skate_auto 173 "$OUT/key-0.png" gl CW_RENDER_SCALE=3 SKATE_PARK=0 SKATE_PLAYERS=1 SKATE_MODE=0 SKATE_NO_HUD=1 SKATE_CAM=0.7,1.4 -- \
  skate_auto 293 "$OUT/key-1.png" gl CW_RENDER_SCALE=3 SKATE_PARK=4 SKATE_PLAYERS=1 SKATE_MODE=0 SKATE_NO_HUD=1 SKATE_CAM=0.7,1.4 -- \
  skate_auto 752 "$OUT/key-2.png" gl CW_RENDER_SCALE=3 SKATE_PARK=2 SKATE_PLAYERS=1 SKATE_MODE=0 SKATE_NO_HUD=1
# the logo: the working title, sunny orange on a dark outline, a little italic lean
convert -size 1600x500 xc:none -font "$FONT" -pointsize 360 -gravity center \
  -stroke "#1a1430" -strokewidth 34 -fill "#1a1430" -annotate 0x-12+0+0 "SKATE" \
  -stroke none -fill "#ffb21c" -annotate 0x-12+0+0 "SKATE" \
  -fill "#ff6a2a" -annotate 0x-12+0+40 "" -trim +repage -bordercolor none -border 24 "$ART/logo.png"
for i in 0 1 2 3; do convert "$OUT/play-$i.png" -resize 1920x1080! -quality 88 "$ART/shot-$((i + 1)).jpg"; done
convert "$OUT/key-1.png" -gravity center -crop 1440x2160+0+0 +repage -resize 1200x1800! -quality 90 "$ART/cover.jpg"
convert "$OUT/key-2.png" -resize 1920x1080! -brightness-contrast -18x-5 -quality 88 "$ART/background.jpg"
convert "$OUT/key-0.png" -resize 1920x1080! -brightness-contrast -8x0 \( -size 1920x420 gradient:"rgba(10,20,60,0.75)"-"rgba(10,20,60,0)" \) -gravity north -composite \( "$ART/logo.png" -resize 1100x \) -gravity north -geometry +0+40 -composite -quality 90 "$ART/title.jpg"
convert "$OUT/key-0.png" -gravity center -crop 1600x1600+0+0 +repage -resize 512x512! "$ART/icon.png"
convert "$OUT/readme.png" -resize 1280x720! -quality 86 "$ROOT/docs/screenshots/skate.jpg"
ls -la "$ART"
