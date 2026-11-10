#!/usr/bin/env bash
# capture-art.sh: the frames tools/make-art.mjs composes the launcher art from (out/art/<name>.png, 3840x2160, no HUD),
# each from the emberthrone_auto CART through romdev (scripts/romdev-shot.mjs: no native game runs for the art), at a
# point of the route (the `from <n>` request) after a number of frames, the art camera (ADV_CAM_*) in the environment
# the cart reads from frame 2. One frame at a time: romdev is shared.
# Names as arguments capture only those: capture-art.sh hero vista
#   scripts/build.sh emberthrone_auto   (TARGETS=cart-wgpu) first; ROMDEV_PORT and CW_GPU_POWER pass through
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
O="$ROOT/out/art"; mkdir -p "$O"
CART="$ROOT/build/cart-wgpu/carts/emberthrone_auto.wasc"
[ -f "$CART" ] || { echo "no $CART: TARGETS=cart-wgpu scripts/build.sh emberthrone_auto" >&2; exit 1; }
WANT=" $* "
shot() { [ "$WANT" = "  " ] || [[ "$WANT" == *" $1 "* ]] || return 0; local name="$1" from="$2" frames="$3"; shift 3
  node "$ROOT/scripts/romdev-shot.mjs" "$CART" "$frames" "$O/$name.png" --size 3840x2160 --cmd "from $from" ADV_NO_HUD=1 "$@" || echo "capture $name failed"; }
shot meadow 1 260
shot fight 6 520
shot marsh 20 330
shot keep 180 260
shot boss 22 700
shot vista 2 200 ADV_CAM_DIST=16 ADV_CAM_PITCH=-0.32
shot hero 1 140 ADV_CAM_KIND=0 ADV_CAM_H=1.3 ADV_CAM_DIST=2.5 ADV_CAM_PITCH=-0.05 ADV_CAM_YAW=3.35
ls "$O"
