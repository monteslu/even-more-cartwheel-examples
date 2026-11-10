#!/usr/bin/env bash
# The launcher art (art/) and the README's screenshot from the game's own frames. Takes the frames through the GPU
# lock (tests/jewels-shot.mjs): play frames with the HUD, key frames without it at 3840x2160 and close views of single
# stones, then crops and composites them with ImageMagick. The logo is the word set in the game's own Cinzel, in gold.
#   games/jewels/tools/make-art.sh [--shots-only | --compose-only]   (the frames and the compositing can run apart)
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
OUT="$ROOT/out/art"; ART="$ROOT/games/jewels/art"; FONT="$ROOT/games/jewels/assets/fonts/Cinzel.ttf"
mkdir -p "$OUT" "$ART"
if [ "${1:-}" != --compose-only ]; then
node "$ROOT/tests/jewels-shot.mjs" \
  jewels_auto 700 "$OUT/play-0.png" gl -- \
  jewels_auto 1460 "$OUT/play-1.png" gl -- \
  jewels_bench 425 "$OUT/play-2.png" gl -- \
  jewels_auto 2300 "$OUT/play-3.png" gl -- \
  jewels_auto 900 "$OUT/key-0.png" gl CW_RENDER_SCALE=2 JEWELS_NO_HUD=1 -- \
  jewels_auto 60 "$OUT/key-1.png" gl CW_RENDER_SCALE=2 JEWELS_NO_HUD=1 JEWELS_SHOWCASE=5 JEWELS_SHOWCASE_SCALE=4.2 -- \
  jewels_auto 60 "$OUT/key-2.png" gl CW_RENDER_SCALE=2 JEWELS_NO_HUD=1 JEWELS_SHOWCASE=3 JEWELS_SHOWCASE_SCALE=4.2
fi
[ "${1:-}" = --shots-only ] && exit 0
# the logo: the name in Cinzel, thickened, pale gold inside a gold rim and a wide dark edge with a drop shadow, on
# transparency: it must read at a launcher grid's size over any picture, and the art check wants it 1280 wide or more
convert -size 2400x720 xc:none -font "$FONT" -pointsize 420 -gravity center \
  -fill "#120a02" -stroke "#120a02" -strokewidth 30 -annotate +10+14 JEWELS \
  -fill "#2e1c06" -stroke "#2e1c06" -strokewidth 44 -annotate +0+0 JEWELS \
  -fill "#b07c22" -stroke "#b07c22" -strokewidth 20 -annotate +0+0 JEWELS \
  -fill "#f6e3a4" -stroke "#f6e3a4" -strokewidth 6 -annotate +0+0 JEWELS \
  -trim +repage -bordercolor none -border 24 "$ART/logo.png"
for i in 0 1 2 3; do convert "$OUT/play-$i.png" -resize 1920x1080! -quality 90 "$ART/shot-$((i + 1)).jpg"; done
convert "$OUT/key-0.png" -crop 1440x2160+624+0 +repage -resize 1200x1800! -quality 92 "$ART/cover.jpg" # over the board
"$ROOT/scripts/cover-name.sh" "$ART/cover.jpg" "$ART/logo.png" # the name across the top
convert "$OUT/key-0.png" -resize 1920x1080! -brightness-contrast -20x-5 -quality 90 "$ART/background.jpg"
convert "$OUT/key-0.png" -resize 1920x1080! -brightness-contrast -30x-5 -fill "rgba(0,0,0,0.6)" -draw "rectangle 0,360 1920,720" \
  \( "$ART/logo.png" -resize 1400x \) -gravity center -composite -quality 92 "$ART/title.jpg"
# the icon: the round brilliant, close
convert "$OUT/key-1.png" -crop 1100x1100+794+530 +repage -resize 512x512! "$ART/icon.png" # the stone sits over the board's middle
convert "$OUT/play-1.png" -resize 1600x900 -quality 88 "$ROOT/docs/screenshots/jewels.jpg"
ls -la "$ART"
