#!/usr/bin/env bash
# The launcher art (art/) from the game's own frames. Takes the frames through the GPU lock (tests/destructo-shot.mjs):
# play frames with the HUD (the bench's keep coming down, aiming at three other levels), and key frames without it
# at 3840x2160, then crops and composites them with ImageMagick. The logo is the title set in Fira Sans Bold
# (SIL Open Font License, from cartwheel-engine's samples).
#   games/destructo/tools/make-art.sh
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
OUT="$ROOT/out/art"; ART="$ROOT/games/destructo/art"
FONT="$ROOT/deps/cartwheel-engine/samples/ui_assets/fonts/FiraSans-Bold.ttf"
mkdir -p "$OUT" "$ART"
node "$ROOT/tests/destructo-shot.mjs" \
  destructo_bench 320 "$OUT/play-0.png" gl CW_RENDER_SCALE=1.5 -- \
  destructo_auto 70 "$OUT/play-1.png" gl CW_RENDER_SCALE=1.5 DESTRUCTO_LEVEL=15 -- \
  destructo_auto 40 "$OUT/play-2.png" gl CW_RENDER_SCALE=1.5 DESTRUCTO_LEVEL=11 -- \
  destructo_bench 470 "$OUT/play-3.png" gl CW_RENDER_SCALE=1.5 -- \
  destructo_bench 316 "$OUT/key-0.png" gl CW_RENDER_SCALE=3 DESTRUCTO_NO_HUD=1 -- \
  destructo_auto 110 "$OUT/key-1.png" gl CW_RENDER_SCALE=3 DESTRUCTO_LEVEL=24 DESTRUCTO_NO_HUD=1
convert -size 1600x420 xc:none -font "$FONT" -pointsize 250 -gravity center \
  -fill '#2a1408' -annotate +10+14 'DESTRUCTO' -stroke '#3a1a08' -strokewidth 12 -fill '#ffb347' -annotate +0+0 'DESTRUCTO' \
  -stroke none -fill '#ffd27a' -annotate +0+0 'DESTRUCTO' -trim +repage "$ART/logo.png"
for i in 0 1 2 3; do convert "$OUT/play-$i.png" -resize 1920x1080! -quality 88 "$ART/shot-$((i + 1)).jpg"; done
convert "$OUT/key-0.png" -gravity center -crop 1440x2160+0+0 +repage -resize 1200x1800! -quality 90 "$ART/cover.jpg"
convert "$OUT/key-1.png" -resize 1920x1080! -brightness-contrast -15x-5 -quality 88 "$ART/background.jpg"
convert "$OUT/key-0.png" -resize 1920x1080! -brightness-contrast -20x-5 -fill "rgba(0,0,0,0.6)" -draw "rectangle 0,380 1920,700" \( "$ART/logo.png" -resize 1500x \) -gravity center -composite -quality 90 "$ART/title.jpg"
convert "$OUT/key-0.png" -gravity center -crop 1400x1400+0-80 +repage -resize 512x512! "$ART/icon.png"
cat > "$ART/art.json" <<'JSON'
{
  "icon": "icon.png",
  "cover": "cover.jpg",
  "background": "background.jpg",
  "logo": "logo.png",
  "title": "title.jpg",
  "screenshots": [
    { "path": "shot-1.jpg", "label": "Great Keep: a charge opens the keep and its storeys come down in planks and stone" },
    { "path": "shot-2.jpg", "label": "Lantern Tower: the aim path climbing toward a six-storey tower in the highlands, its crystals' beacons over the battlements" },
    { "path": "shot-3.jpg", "label": "Sun Steps: a stepped pyramid with a gilded shrine among the desert mesas" },
    { "path": "shot-4.jpg", "label": "Great Keep settling: the dust drifts and the debris comes to rest" }
  ]
}
JSON
convert "$OUT/play-0.png" -resize 1280x720! -quality 85 "$ROOT/docs/screenshots/destructo.jpg" # the README's picture
ls -la "$ART"
