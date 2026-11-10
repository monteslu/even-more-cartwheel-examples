#!/usr/bin/env bash
# The launcher art (art/) from the game's own frames. Takes the frames through the GPU lock (tests/skate-shot.mjs):
# play frames with the HUD (two players in split screen on four parks), and key frames without it at 3840x2160 (one
# skater), then crops and composites them with ImageMagick. The logo is the working title set in the engine's sans.
#   games/skate/tools/make-art.sh [--compose-only]   (--compose-only: the frames already in out/art)
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
OUT="$ROOT/out/art"; ART="$ROOT/games/skate/art"
FONT="$ROOT/deps/cartwheel-engine/src/ui/fonts/FiraSans-Regular.ttf"
mkdir -p "$OUT" "$ART"
[ "${1:-}" = --compose-only ] || node "$ROOT/tests/skate-shot.mjs" \
  skate_auto 727 "$OUT/play-0.png" gl CW_RENDER_SCALE=1.5 SKATE_PARK=0 -- \
  skate_auto 742 "$OUT/play-1.png" gl CW_RENDER_SCALE=1.5 SKATE_PARK=2 -- \
  skate_auto 662 "$OUT/play-2.png" gl CW_RENDER_SCALE=1.5 SKATE_PARK=1 -- \
  skate_auto 800 "$OUT/play-3.png" gl CW_RENDER_SCALE=1.5 SKATE_PARK=3 -- \
  skate_auto 900 "$OUT/readme.png" gl CW_RENDER_SCALE=1.5 SKATE_PARK=4 SKATE_PLAYERS=4 -- \
  skate_auto 220 "$OUT/key-0.png" gl CW_RENDER_SCALE=3 SKATE_PARK=0 SKATE_PLAYERS=1 SKATE_MODE=0 SKATE_NO_HUD=1 SKATE_CAM=0.7,1.4 -- \
  skate_auto 500 "$OUT/key-1.png" gl CW_RENDER_SCALE=3 SKATE_PARK=4 SKATE_PLAYERS=1 SKATE_MODE=0 SKATE_NO_HUD=1 SKATE_CAM=0.7,1.4 -- \
  skate_auto 752 "$OUT/key-2.png" gl CW_RENDER_SCALE=3 SKATE_PARK=2 SKATE_PLAYERS=1 SKATE_MODE=0 SKATE_NO_HUD=1
# the logo: the working title, a little italic lean, thickened: sunny yellow with an orange rim inside a wide dark
# outline and a drop shadow, so it reads at a launcher grid's size over any picture (the art check wants 1280 wide or more)
convert -size 2400x900 xc:none -font "$FONT" -pointsize 480 -gravity center \
  -fill "#0b0818" -stroke "#0b0818" -strokewidth 46 -annotate 0x-12+14+20 "SKATE" \
  -fill "#1a1430" -stroke "#1a1430" -strokewidth 56 -annotate 0x-12+0+0 "SKATE" \
  -fill "#ff8a1c" -stroke "#ff8a1c" -strokewidth 26 -annotate 0x-12+0+0 "SKATE" \
  -fill "#ffc21c" -stroke "#ffc21c" -strokewidth 14 -annotate 0x-12+0+0 "SKATE" \
  -trim +repage -bordercolor none -border 24 "$ART/logo.png"
for i in 0 1 2 3; do convert "$OUT/play-$i.png" -resize 1920x1080! -quality 88 "$ART/shot-$((i + 1)).jpg"; done
convert "$OUT/key-1.png" -gravity center -crop 1440x2160+0+0 +repage -resize 1200x1800! -quality 90 "$ART/cover.jpg"
"$ROOT/scripts/cover-name.sh" "$ART/cover.jpg" "$ART/logo.png" # the name across the top
convert "$OUT/key-2.png" -resize 1920x1080! -brightness-contrast -18x-5 -quality 88 "$ART/background.jpg"
convert "$OUT/key-0.png" -resize 1920x1080! -brightness-contrast -8x0 \( -size 1920x420 gradient:"rgba(10,20,60,0.75)"-"rgba(10,20,60,0)" \) -gravity north -composite \( "$ART/logo.png" -resize 1100x \) -gravity north -geometry +0+40 -composite -quality 90 "$ART/title.jpg"
convert "$OUT/key-0.png" -gravity center -crop 1600x1600+0+0 +repage -resize 512x512! "$ART/icon.png"
convert "$OUT/readme.png" -resize 1280x720! -quality 86 "$ROOT/docs/screenshots/skate.jpg"
ls -la "$ART"
