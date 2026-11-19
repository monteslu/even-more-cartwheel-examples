#!/usr/bin/env bash
# The launcher art (art/) from the game's own frames, taken through romdev at 3840x2160 by tests/beatsync-art-shots.mjs
# (a song on each difficulty played on a pad, and a whole song to its results), then cropped and composited with
# ImageMagick. The logo is the title set in Fira Mono (SIL Open Font License, the font the game's characters are drawn
# in), magenta with a cyan edge and a soft glow. Run it inside the GPU suite lock:
#   node deps/cartwheel-engine/tools/with-gpu-suite.mjs beatsync-art games/beatsync/tools/make-art.sh
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
OUT="$ROOT/out/gate/beatsync-art"; ART="$ROOT/games/beatsync/art"
FONT="$ROOT/games/beatsync/fonts/FiraMono-Regular.ttf"
mkdir -p "$OUT" "$ART"
[ -n "${BEATSYNC_ART_REUSE:-}" ] || node "$ROOT/tests/beatsync-art-shots.mjs" "$OUT"
convert -size 1900x460 xc:none -font "$FONT" -pointsize 260 -gravity center \
  \( +clone -fill '#ff2fd0' -annotate +0+0 'beatsync' -blur 0x22 \) -composite \
  -stroke '#36f2ff' -strokewidth 10 -fill none -annotate +0+0 'beatsync' \
  -stroke none -fill '#ffd6f6' -annotate +0+0 'beatsync' -trim +repage "$ART/logo.png"
i=1
for s in play-easy play-medium play-hard results; do convert "$OUT/$s.png" -resize 1920x1080! -quality 88 "$ART/shot-$i.jpg"; i=$((i + 1)); done
convert "$OUT/play-hard2.png" -gravity center -crop 1440x2160+0+0 +repage -resize 1200x1800! -quality 90 "$ART/cover.jpg"
"$ROOT/scripts/cover-name.sh" "$ART/cover.jpg" "$ART/logo.png" # the name across the top
convert "$OUT/play-hard2.png" -resize 1920x1080! -brightness-contrast -15x0 -quality 88 "$ART/background.jpg"
convert "$OUT/play-hard.png" -resize 1920x1080! -brightness-contrast -20x-5 -fill "rgba(0,0,0,0.6)" -draw "rectangle 0,380 1920,700" \( "$ART/logo.png" -resize 1500x \) -gravity center -composite -quality 90 "$ART/title.jpg"
convert "$OUT/play-hard.png" -gravity center -crop 1500x1500+0+0 +repage -resize 512x512! "$ART/icon.png"
cat > "$ART/art.json" <<'JSON'
{
  "icon": "icon.png",
  "cover": "cover.jpg",
  "background": "background.jpg",
  "logo": "logo.png",
  "title": "title.jpg",
  "screenshots": [
    { "path": "shot-1.jpg", "label": "Late Night on Easy: the three rings turning with the song, the bass waveform along the bottom" },
    { "path": "shot-2.jpg", "label": "Chicago Jack on Medium: a streak lighting the field" },
    { "path": "shot-3.jpg", "label": "Acid House Rave on Hard, in the drop" },
    { "path": "shot-4.jpg", "label": "The results: the grade, the score and the best" }
  ]
}
JSON
convert "$OUT/play-hard.png" -resize 1280x720! -quality 85 "$ROOT/docs/screenshots/beatsync.jpg" # the README's picture
ls -la "$ART"
