#!/usr/bin/env bash
# survey.sh: the program table from runs on software GL (off the shared GPU): adventure_auto at every graphics preset
# from several points of the route (each region and each boss fight), the states each run reports merged into
# programs/adventure.txt and the C regenerated (tools/programs.mjs --no-run --logs; Tint checks the WebGPU programs).
# The shipping cart's own states are surveyed on the GPU with tools/programs.mjs --carts (see the README).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
L="$ROOT/out/proglogs"
rm -rf "$L"; mkdir -p "$L/all"
for q in 0 1 2 3 4; do for f in 0 7 12 19 22 30 37 40; do echo "$q $f"; done; done |
  xargs -P 4 -n 2 sh -c 'mkdir -p "'"$L"'/q$0f$1"; CW_PROGRAMS_LOG_DIR="'"$L"'/q$0f$1" ADV_QUALITY=$0 ADV_FROM=$1 timeout 300 "'"$ROOT"'/games/adventure/tools/swshot.sh" adventure_auto 300 /dev/null >/dev/null 2>&1 || true'
cat "$L"/q*/adventure_auto.log 2>/dev/null | sort -u > "$L/all/adventure_auto.log"
echo "$(wc -l < "$L/all/adventure_auto.log") states reported"
WEBGPU_NODE="${WEBGPU_NODE:-$(ls -d "$ROOT"/../../../wasmcart/node_modules/webgpu-node 2>/dev/null || true)}" CW_PROJECT_ROOT="$ROOT" \
  node "$ROOT/deps/cartwheel-engine/tools/programs.mjs" adventure_auto --no-run --build "$ROOT/build/native" --logs "$L/all"
