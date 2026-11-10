#!/usr/bin/env bash
# The launcher art (art/) from the game's own frames: rooms of the regions and a warden fight, drawn by the
# lanternfall_auto and lanternfall_bench CARTS through romdev (scripts/romdev-shot.mjs: no native game runs for the
# art), one frame at a time. The auto cart answers `room <n>` (the room), and reads LANTERNFALL_IDLE (the controls
# left alone), LANTERNFALL_NO_HUD (the key art), LANTERNFALL_ART_ZOOM and LANTERNFALL_ART_UP (the cover's camera: k
# times closer on the lamplighter, looking t tiles above) from the environment a cart host delivers at frame 2. The
# frames are cropped and composited with ImageMagick; the logo is the name set in DejaVu Sans Bold.
#   games/lanternfall/tools/make-art.sh [--compose-only]   (--compose-only: the frames already in out/art-lanternfall)
#   TARGETS=cart-wgpu scripts/build.sh lanternfall_auto lanternfall_bench   first; ROMDEV_PORT and CW_GPU_POWER pass through
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
OUT="$ROOT/out/art-lanternfall"; ART="$ROOT/games/lanternfall/art"
CARTS="$ROOT/build/cart-wgpu/carts"
mkdir -p "$OUT" "$ART"
shot() { # <app> <frames> <out> <size> [--cmd "<request>"] [ENV=v ...]
  local app=$1 frames=$2 out=$3 size=$4; shift 4
  [ -f "$CARTS/$app.wasc" ] || { echo "no $CARTS/$app.wasc: TARGETS=cart-wgpu scripts/build.sh $app" >&2; exit 1; }
  node "$ROOT/scripts/romdev-shot.mjs" "$CARTS/$app.wasc" "$frames" "$out" --size "$size" "$@"
}
if [ "${1:-}" != --compose-only ]; then
  shot lanternfall_auto 900 "$OUT/play-0.png" 2880x1620
  shot lanternfall_auto 70 "$OUT/play-1.png" 2880x1620 --cmd "room 288" LANTERNFALL_IDLE=1
  shot lanternfall_auto 70 "$OUT/play-2.png" 2880x1620 --cmd "room 171" LANTERNFALL_IDLE=1
  shot lanternfall_bench 300 "$OUT/play-3.png" 2880x1620
  shot lanternfall_auto 70 "$OUT/key-0.png" 2880x1620 --cmd "room 335" LANTERNFALL_IDLE=1 LANTERNFALL_NO_HUD=1
  shot lanternfall_auto 70 "$OUT/key-1.png" 2880x1620 --cmd "room 137" LANTERNFALL_IDLE=1 LANTERNFALL_NO_HUD=1
  # the cover's frame: the Lantern Gate by a lit lamp post, the lamplighter large, the towers of the old city behind
  shot lanternfall_auto 90 "$OUT/key-2.png" 3840x2160 --cmd "room ${LANTERNFALL_ART_ROOM:-146}" LANTERNFALL_IDLE=1 LANTERNFALL_NO_HUD=1 \
    LANTERNFALL_ART_ZOOM="${LANTERNFALL_ART_ZOOM:-3.6}" LANTERNFALL_ART_UP="${LANTERNFALL_ART_UP:-1.0}"
fi
F="$(fc-match -f '%{file}' 'DejaVu Sans:bold')"
convert -size 2800x400 xc:none -font "$F" -pointsize 260 -fill "#ffe6a8" -stroke "#3a2a1a" -strokewidth 10 -gravity center \
  -annotate +0+0 "LANTERNFALL" -trim +repage "$ART/logo.png"
for i in 0 1 2 3; do convert "$OUT/play-$i.png" -resize 1920x1080! -quality 88 "$ART/shot-$((i + 1)).jpg"; done
# the cover: the middle of the 4K frame at 2:3 (1200x1800), the sky's top a step darker toward dusk, the name across it
convert "$OUT/key-2.png" -gravity center -crop 1440x2160+0+0 +repage -resize 1200x1800! -quality 90 "$ART/cover.jpg"
"$ROOT/scripts/cover-name.sh" "$ART/cover.jpg" "$ART/logo.png" # the name across the top
convert "$OUT/key-1.png" -resize 1920x1080! -brightness-contrast -18x-5 -quality 88 "$ART/background.jpg"
convert "$OUT/key-0.png" -resize 1920x1080! -brightness-contrast -22x-5 \( "$ART/logo.png" -resize 1400x \) -gravity center -composite \
  -quality 90 "$ART/title.jpg"
convert "$OUT/key-1.png" -resize 1920x1080! -gravity center -crop 1000x1000+0+0 +repage -resize 512x512! "$ART/icon.png"
ls -la "$ART"
