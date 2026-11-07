#!/usr/bin/env bash
# The launcher art (art/) from the game's own frames: rooms of six regions and a warden fight, drawn by explorer_auto
# and explorer_bench (EXPLORER_ROOM picks the room, EXPLORER_IDLE leaves the controls alone, EXPLORER_NO_HUD hides the
# HUD for the key art), cropped and composited with ImageMagick; the logo is the working title set in DejaVu Sans Bold.
# Frames come through the GPU suite lock on the test GPU; EXPLORER_ART_SOFTWARE=1 draws them on Mesa's software
# Vulkan (lavapipe) instead, off the GPU.
#   games/explorer/tools/make-art.sh
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
OUT="$ROOT/out/art-explorer"; ART="$ROOT/games/explorer/art"
BIN="$ROOT/build/native/project/games/explorer"
[ -x "$BIN/explorer_auto" ] || BIN="$ROOT/build/native/games/explorer"
mkdir -p "$OUT" "$ART"
shot() { # <app> <frames> <out> <scale> [ENV=v ...]
  local app=$1 frames=$2 out=$3 scale=$4; shift 4
  if [ -n "${EXPLORER_ART_SOFTWARE:-}" ]; then
    env -u DISPLAY -u WAYLAND_DISPLAY -u XAUTHORITY SDL_VIDEODRIVER=offscreen VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json \
      CW_GPU=webgpu CW_FRAMES="$frames" CW_WARMUP_SPREAD=0 SDL_AUDIODRIVER=dummy CW_RENDER_SCALE="$scale" CW_SCREENSHOT="$out" "$@" \
      "$BIN/$app" >/dev/null 2>&1
  else
    env CW_GPU=gl CW_FRAMES="$frames" CW_WARMUP_SPREAD=0 SDL_AUDIODRIVER=dummy CW_RENDER_SCALE="$scale" CW_SCREENSHOT="$out" "$@" \
      node "$ROOT/deps/cartwheel-engine/tools/with-gpu-suite.mjs" "explorer-art" "$BIN/$app" >/dev/null
  fi
}
shot explorer_auto 900 "$OUT/play-0.png" 1.5
shot explorer_auto 70 "$OUT/play-1.png" 1.5 EXPLORER_IDLE=1 EXPLORER_ROOM=288
shot explorer_auto 70 "$OUT/play-2.png" 1.5 EXPLORER_IDLE=1 EXPLORER_ROOM=171
shot explorer_bench 300 "$OUT/play-3.png" 1.5
shot explorer_auto 70 "$OUT/key-0.png" 1.5 EXPLORER_IDLE=1 EXPLORER_ROOM=335 EXPLORER_NO_HUD=1
shot explorer_auto 70 "$OUT/key-1.png" 1.5 EXPLORER_IDLE=1 EXPLORER_ROOM=137 EXPLORER_NO_HUD=1
F="$(fc-match -f '%{file}' 'DejaVu Sans:bold')"
convert -size 1600x400 xc:none -font "$F" -pointsize 260 -fill "#ffe6a8" -stroke "#3a2a1a" -strokewidth 10 -gravity center \
  -annotate +0+0 "EXPLORER" -trim +repage "$ART/logo.png"
for i in 0 1 2 3; do convert "$OUT/play-$i.png" -resize 1920x1080! -quality 88 "$ART/shot-$((i + 1)).jpg"; done
convert "$OUT/key-0.png" -gravity center -crop 720x1080+0+0 +repage -resize 800x1200! -quality 90 "$ART/cover.jpg"
convert "$OUT/key-1.png" -resize 1920x1080! -brightness-contrast -18x-5 -quality 88 "$ART/background.jpg"
convert "$OUT/key-0.png" -resize 1920x1080! -brightness-contrast -22x-5 \( "$ART/logo.png" -resize 1400x \) -gravity center -composite \
  -quality 90 "$ART/title.jpg"
convert "$OUT/key-1.png" -gravity center -crop 1000x1000+0+0 +repage -resize 512x512! "$ART/icon.png"
ls -la "$ART"
