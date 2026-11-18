#!/usr/bin/env bash
# The launcher art (art/) from the game's own frames. Takes the frames through romdev (tests/detexto-art-shots.mjs):
# play frames with the HUD (four landmarks mid-collapse), and key frames without it
# at 3840x2160, then crops and composites them with ImageMagick. The logo is the title set in Fira Mono (SIL Open
# Font License, the font the game's characters are drawn in), neon magenta with a cyan edge and a soft glow.
#   games/detexto/tools/make-art.sh
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
OUT="$ROOT/out/gate/art"; ART="$ROOT/games/detexto/art"
FONT="$ROOT/games/detexto/assets/fonts/FiraMono-Regular.ttf"
mkdir -p "$OUT" "$ART"
node "$ROOT/tests/detexto-art-shots.mjs" "$OUT"
convert -size 1700x460 xc:none -font "$FONT" -pointsize 240 -gravity center \
  \( +clone -fill '#ff2fd0' -annotate +0+0 'DETEXTO' -blur 0x22 \) -composite \
  -stroke '#36f2ff' -strokewidth 10 -fill none -annotate +0+0 'DETEXTO' \
  -stroke none -fill '#ffd6f6' -annotate +0+0 'DETEXTO' -trim +repage "$ART/logo.png"
for i in 0 1 2 3; do convert "$OUT/play-$i.png" -resize 1920x1080! -quality 88 "$ART/shot-$((i + 1)).jpg"; done
convert "$OUT/key-0.png" -gravity center -crop 1440x2160+0+0 +repage -resize 1200x1800! -quality 90 "$ART/cover.jpg"
"$ROOT/scripts/cover-name.sh" "$ART/cover.jpg" "$ART/logo.png" # the name across the top
convert "$OUT/key-1.png" -resize 1920x1080! -brightness-contrast -5x0 -quality 88 "$ART/background.jpg"
convert "$OUT/key-2.png" -resize 1920x1080! -brightness-contrast -20x-5 -fill "rgba(0,0,0,0.6)" -draw "rectangle 0,380 1920,700" \( "$ART/logo.png" -resize 1500x \) -gravity center -composite -quality 90 "$ART/title.jpg"
convert "$OUT/key-3.png" -gravity center -crop 1400x1400+0-80 +repage -resize 512x512! "$ART/icon.png"
cat > "$ART/art.json" <<'JSON'
{
  "icon": "icon.png",
  "cover": "cover.jpg",
  "background": "background.jpg",
  "logo": "logo.png",
  "title": "title.jpg",
  "screenshots": [
    { "path": "shot-1.jpg", "label": "Hoover Dam in neon text: a breach opens and the lake spills through" },
    { "path": "shot-2.jpg", "label": "Taj Mahal in neon text: the dome opens and the marble comes down" },
    { "path": "shot-3.jpg", "label": "Mount Rushmore in neon text: a charge goes off at the foot of the carved faces" },
    { "path": "shot-4.jpg", "label": "Iron Lady in neon text: the shot strikes the lattice tower" }
  ]
}
JSON
convert "$OUT/play-0.png" -resize 1280x720! -quality 85 "$ROOT/docs/screenshots/detexto.jpg" # the README's picture
ls -la "$ART"
