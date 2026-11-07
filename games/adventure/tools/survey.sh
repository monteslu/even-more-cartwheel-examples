#!/usr/bin/env bash
# survey.sh: the program table from runs on software GL (off the shared GPU): adventure_auto at every graphics preset
# from several points of the route (each region and each boss fight), plus the perf probe's variants, the states each
# run reports merged into programs/adventure.txt and the C regenerated (tools/programs.mjs --no-run --logs; Tint
# checks the WebGPU programs). The runs draw at a quarter of the size (the states do not depend on it).
# The shipping cart's own states are surveyed on the GPU with tools/programs.mjs --carts (see the README).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
L="$ROOT/out/proglogs"
mkdir -p "$L/all"
RUN() { # <dir> <frames> ENV=V...
  local d="$1" fr="$2"; shift 2
  [ -s "$L/$d/done" ] && return 0
  mkdir -p "$L/$d"
  CW_PROGRAMS_LOG_DIR="$L/$d" CW_RENDER_SCALE=0.25 timeout 300 "$ROOT/games/adventure/tools/swshot.sh" adventure_auto "$fr" /dev/null "$@" >/dev/null 2>&1 || true
  echo ok > "$L/$d/done"
}
export -f RUN; export L ROOT
{
  for q in 0 1 2 3 4; do for f in 0 7 12 22 28 38 44 58 64 100 160 182; do echo "q${q}f$f 200 ADV_QUALITY=$q ADV_FROM=$f"; done; done
  for f in 1 12 22; do for p in NOSHADOW NOTREES NOCLOUDS; do echo "p${p}f$f 150 ADV_QUALITY=3 ADV_FROM=$f ADV_PROBE_$p=1"; done; done
} | xargs -P 4 -L 1 bash -c 'RUN "$@"' _
cat "$L"/[qp]*/adventure_auto.log 2>/dev/null | sort -u > "$L/all/adventure_auto.log"
echo "$(wc -l < "$L/all/adventure_auto.log") states reported"
WEBGPU_NODE="${WEBGPU_NODE:-$(ls -d "$ROOT"/../../../wasmcart/node_modules/webgpu-node 2>/dev/null || true)}" CW_PROJECT_ROOT="$ROOT" \
  node "$ROOT/deps/cartwheel-engine/tools/programs.mjs" adventure_auto --no-run --build "$ROOT/build/native" --logs "$L/all"
