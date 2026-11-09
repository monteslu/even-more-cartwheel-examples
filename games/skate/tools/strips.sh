#!/usr/bin/env bash
# The posing's review strips (games/skate/tools/strip.mjs) for every state, into out/strips/<set>/:
#   games/skate/tools/strips.sh <set> [state ...]
# States: push, carve, ollie, flip, grab, manual (a scripted pad on Sunnyside's flat, tools/pads/), grind, vert, bail
# (a bot), runout (a forced run-out after the pushes), goofy (the states again with the right foot forward). Views: chase (behind the run: the rider's side), face (ahead of it), toe (the toe side), q (three quarters).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
SET="${1:?set name}"; shift
O="$ROOT/out/strips/$SET"; mkdir -p "$O"
STATES=("$@")
want() { [ ${#STATES[@]} -eq 0 ] && return 0; for s in "${STATES[@]}"; do [ "$s" = "$1" ] && return 0; done; return 1; }
B="SKATE_PLAYERS=1 SKATE_MODE=0 CW_RENDER_SCALE=1.5 CROP=440x640"
pad() { echo "$B SKATE_PAD=$ROOT/games/skate/tools/pads/$1.txt"; }
TOE="SKATE_VIEWCAM=90,6,5,40"; CHASE="SKATE_VIEWCAM=0,8,5,40"; FACE="SKATE_VIEWCAM=180,8,5,40"; Q="SKATE_VIEWCAM=45,10,5,40"
A=()
add() { [ ${#A[@]} -gt 0 ] && A+=(--); A+=("$@"); }
if want push; then add "$O/push-chase.png" skate_auto 24:70:3 $(pad push) $CHASE; add "$O/push-toe.png" skate_auto 24:70:3 $(pad push) $TOE; add "$O/cruise.png" skate_auto 150:174:12 $(pad push) $Q; fi
if want carve; then add "$O/carve-face.png" skate_auto 100:196:8 $(pad carve) $FACE; add "$O/carve-chase.png" skate_auto 100:196:8 $(pad carve) $CHASE; fi
if want ollie; then add "$O/ollie-chase.png" skate_auto 104:182:6 $(pad ollie) $CHASE; add "$O/ollie-toe.png" skate_auto 104:182:6 $(pad ollie) $TOE; add "$O/land-chase.png" skate_auto 160:196:3 $(pad ollie) $CHASE; fi
if want flip; then add "$O/flip-chase.png" skate_auto 116:170:3 $(pad flip) $CHASE; add "$O/flip-q.png" skate_auto 116:170:3 $(pad flip) $Q; add "$O/heelflip-q.png" skate_auto 116:170:3 $(pad heelflip) $Q; fi
if want grab; then add "$O/grab-q.png" skate_auto 126:186:5 $(pad grab) $Q; add "$O/grab-chase.png" skate_auto 126:186:5 $(pad grab) $CHASE; fi
if want manual; then add "$O/manual-chase.png" skate_auto 104:164:5 $(pad manual) $CHASE; add "$O/manual-toe.png" skate_auto 104:164:5 $(pad manual) $TOE; fi
if want grind; then add "$O/grind-chase.png" skate_auto find2@1:24:60:6 $B SKATE_LINE=6 $CHASE; add "$O/grind-q.png" skate_auto find2@1:24:60:6 $B SKATE_LINE=6 $Q; fi
if want vert; then add "$O/vert-side.png" skate_auto find1@1:12:84:6 $B SKATE_LINE=2 SKATE_VIEWCAM=90,4,7,50 CROP=600x900; fi
if want runout; then add "$O/runout-q.png" skate_auto 116:184:4 $(pad push) SKATE_RUNOUT_AT=120 $Q CROP=600x640; add "$O/runout-toe.png" skate_auto 116:184:4 $(pad push) SKATE_RUNOUT_AT=120 $TOE CROP=600x640; fi
G="SKATE_GOOFY=1"
if want goofy; then
  add "$O/goofy-push-toe.png" skate_auto 24:70:3 $(pad push) $G $TOE; add "$O/goofy-cruise.png" skate_auto 150:174:12 $(pad push) $G $Q
  add "$O/goofy-ollie-toe.png" skate_auto 104:182:6 $(pad ollie) $G $TOE; add "$O/goofy-flip-q.png" skate_auto 116:170:3 $(pad flip) $G $Q
  add "$O/goofy-carve-face.png" skate_auto 100:196:8 $(pad carve) $G $FACE; add "$O/goofy-grab-q.png" skate_auto 126:186:5 $(pad grab) $G $Q
  add "$O/goofy-manual-toe.png" skate_auto 104:164:5 $(pad manual) $G $TOE; add "$O/goofy-grind-q.png" skate_auto find2@1:24:60:6 $B SKATE_LINE=6 $G $Q
  add "$O/regular-push-toe.png" skate_auto 24:70:3 $(pad push) SKATE_GOOFY=0 $TOE
fi
if want bail; then add "$O/bail.png" skate_auto 196:268:6 $B SKATE_BAIL_AT=200 SKATE_VIEWCAM=60,10,6,45 CROP=600x640; fi
node "$ROOT/games/skate/tools/strip.mjs" "${A[@]}"
