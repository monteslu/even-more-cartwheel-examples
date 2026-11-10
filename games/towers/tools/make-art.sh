#!/usr/bin/env bash
# The cover (art/cover.jpg) from the game's own frame: the towers_auto CART through romdev (scripts/romdev-shot.mjs; no
# native game runs for the art), a late wave of the volcano held by towers on every slot, the camera brought in on the
# crowd marching into the beams (the `map`, `cam` requests; TOWERS_WAVE, TOWERS_NO_HUD and TOWERS_NO_CURSORS in the
# environment the cart reads from frame 2), 3840x2160, the middle cropped to 2:3 at 1200x1800 and the name laid across
# the top. The screenshots, background, title and icon are tests/towers-shots.mjs's and the native look's.
#   games/towers/tools/make-art.sh [--compose-only]   (--compose-only: the frame already in out/art)
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
OUT="$ROOT/out/art"; ART="$ROOT/games/towers/art"
CART="$ROOT/build/cart-wgpu/carts/towers_auto.wasc"
mkdir -p "$OUT" "$ART"
if [ "${1:-}" != --compose-only ]; then
  [ -f "$CART" ] || { echo "no $CART: TARGETS=cart-wgpu scripts/build.sh towers_auto" >&2; exit 1; }
  node "$ROOT/scripts/romdev-shot.mjs" "$CART" "${TOWERS_ART_FRAMES:-1500}" "$OUT/towers-key.png" --size 3840x2160 \
    --cmd "map ${TOWERS_ART_MAP:-23}" --cmd "cam ${TOWERS_ART_CAM:-0.62 4 -11}" TOWERS_NO_HUD=1 TOWERS_NO_CURSORS=1 TOWERS_WAVE=18
fi
convert "$OUT/towers-key.png" -gravity center -crop 1440x2160+0+0 +repage -resize 1200x1800! -quality 90 "$ART/cover.jpg"
"$ROOT/scripts/cover-name.sh" "$ART/cover.jpg" "$ART/logo.png" # the name across the top
ls -la "$ART/cover.jpg"
