#!/usr/bin/env bash
# The launcher art (art/) from the game's own frames. Takes the frames through romdev (tests/detexto-art-shots.mjs):
# play frames with the HUD (four landmarks mid-collapse), and key frames without it
# at 3840x2160, then crops and composites them with ImageMagick. The frames are in the default look, one bit a pixel:
# they are scaled by sampling (every pixel kept black or white, never averaged to grey). The logo is the title set in
# Fira Mono (SIL Open Font License, the font of the ASCII look), white with a black edge and a soft white glow.
#   games/detexto/tools/make-art.sh
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
OUT="$ROOT/out/gate/art"; ART="$ROOT/games/detexto/art"
FONT="$ROOT/games/detexto/assets/fonts/FiraMono-Regular.ttf"
mkdir -p "$OUT" "$ART"
node "$ROOT/tests/detexto-art-shots.mjs" "$OUT"
convert -size 1700x460 xc:none -font "$FONT" -pointsize 240 -gravity center \
  \( +clone -fill '#ffffff' -annotate +0+0 'DETEXTO' -blur 0x22 \) -composite \
  -stroke '#000000' -strokewidth 10 -fill none -annotate +0+0 'DETEXTO' \
  -stroke none -fill '#ffffff' -annotate +0+0 'DETEXTO' -trim +repage "$ART/logo.png"
for i in 0 1 2 3; do convert "$OUT/play-$i.png" -sample 1920x1080! -quality 92 "$ART/shot-$((i + 1)).jpg"; done
convert "$OUT/key-0.png" -gravity center -crop 1440x2160+0+0 +repage -sample 1200x1800! -quality 92 "$ART/cover.jpg"
"$ROOT/scripts/cover-name.sh" "$ART/cover.jpg" "$ART/logo.png" # the name across the top
convert "$OUT/key-1.png" -sample 1920x1080! -quality 92 "$ART/background.jpg"
convert "$OUT/key-2.png" -sample 1920x1080! -brightness-contrast -20x-5 -fill "rgba(0,0,0,0.6)" -draw "rectangle 0,380 1920,700" \( "$ART/logo.png" -resize 1500x \) -gravity center -composite -quality 90 "$ART/title.jpg"
convert "$OUT/key-3.png" -gravity center -crop 1400x1400+0-80 +repage -filter box -resize 512x512! "$ART/icon.png" # averaged: at an icon's size the dots read as grey
cat > "$ART/art.json" <<'JSON'
{
  "icon": "icon.png",
  "cover": "cover.jpg",
  "background": "background.jpg",
  "logo": "logo.png",
  "title": "title.jpg",
  "screenshots": [
    { "path": "shot-1.jpg", "label": "Hoover Dam in one bit: a breach opens and the lake spills through" },
    { "path": "shot-2.jpg", "label": "Taj Mahal in one bit: the dome opens and the marble comes down" },
    { "path": "shot-3.jpg", "label": "Mount Rushmore in one bit: a charge goes off at the foot of the carved faces" },
    { "path": "shot-4.jpg", "label": "Iron Lady in one bit: the shot strikes the lattice tower" }
  ]
}
JSON
convert "$OUT/play-0.png" -sample 1280x720! -quality 92 "$ROOT/docs/screenshots/detexto.jpg" # the README's picture
ls -la "$ART"
