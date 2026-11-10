#!/usr/bin/env bash
# The game's name across the top of its cover (art.cover is box art: the name must read at a launcher's grid size).
# Darkens the top of the cover a little so any logo reads over any frame, then lays the logo over it, 86% of the
# cover's width (no taller than a fifth of it), centred near the top. Run by each game's art script after it writes
# the cover:
#   scripts/cover-name.sh games/<game>/art/cover.jpg games/<game>/art/logo.png
set -euo pipefail
COVER="$1"; LOGO="$2"
read -r W H < <(identify -format '%w %h\n' "$COVER")
LW=$((W * 86 / 100)); LH=$((H / 5)); TOP=$((H * 5 / 100)); BAND=$((H * 36 / 100))
convert "$COVER" \
  \( -size "${W}x${BAND}" gradient:'rgba(0,0,0,0.62)-rgba(0,0,0,0)' \) -gravity north -composite \
  \( "$LOGO" -resize "${LW}x${LH}" \) -gravity north -geometry "+0+${TOP}" -composite \
  -quality 90 "$COVER.tmp.jpg"
mv "$COVER.tmp.jpg" "$COVER"
