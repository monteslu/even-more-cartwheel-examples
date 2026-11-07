#!/usr/bin/env bash
# motion-sheet.sh <app> <input.json|-> <out.png> <frame> [frame ...]: a contact sheet of one run seen at several
# frames (the runs are deterministic, so each frame is the same run stopped there), on software GL (llvmpipe, off the
# shared GPU): how a move looks in motion from a pad script (tests/emberthrone-controls.json, in the gate runner's JSON
# format) or the bot ("-"), a frame or a few apart. Extra environment passes through (ADV_FROM, ADV_NO_HUD, ...).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
# one GPU suite lock for the whole batch (software GL, but it loads the machine: the quality timing runs need it quiet), two shots at a time
if [ -z "${CW_GPU_SUITE_INNER:-}" ]; then CW_GPU_SUITE_INNER=1 exec node "$ROOT/deps/cartwheel-engine/tools/with-gpu-suite.mjs" emberthrone-motion-sheet bash "${BASH_SOURCE[0]}" "$@"; fi
APP="$1"; IN="$2"; OUT="$3"; shift 3
D="$ROOT/out/motion/$(basename "$OUT" .png)"; rm -rf "$D"; mkdir -p "$D"
EXTRA=()
if [ "$IN" != "-" ]; then
  # the native input script: one line per entry, "<frame-1> <button mask> lx ly rx ry lt rt" (tools/run-cart.mjs)
  node -e '
    const B = { south: 0, east: 1, west: 2, north: 3, back: 4, guide: 5, start: 6, l3: 7, r3: 8, l: 9, r: 10, up: 11, down: 12, left: 13, right: 14 };
    const ev = JSON.parse(require("fs").readFileSync(process.argv[1], "utf8"));
    const out = ev.map((s) => [s.frame - 1, (s.buttons || []).reduce((m, n) => m | (1 << B[n]), 0), ...["lx", "ly", "rx", "ry", "lt", "rt"].map((a) => (s.axes || {})[a] || 0)].join(" "));
    require("fs").writeFileSync(process.argv[2], out.join("\n") + "\n");' "$IN" "$D/input.txt"
  EXTRA=(CW_INPUT="$D/input.txt")
fi
for f in "$@"; do echo "$f"; done | xargs -P 2 -I{} sh -c '"'"$ROOT"'/games/emberthrone/tools/swshot.sh" "'"$APP"'" {} "'"$D"'/f$(printf %05d {}).png" '"${EXTRA[*]:-}"' >/dev/null 2>&1 || true'
montage "$D"/f*.png -tile 4x -geometry 640x360+4+4 -background '#111' "$OUT"
echo "$OUT"
