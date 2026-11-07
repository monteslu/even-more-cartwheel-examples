#!/usr/bin/env bash
# survey-probe.sh <PROBE> [from ...]: the program states a perf probe variant needs (ADV_PROBE_<PROBE>), logged into
# out/proglogs/probe-<PROBE>/ from short runs at High (development: add them to the table only to measure the variant)
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
P="$1"; shift
for f in "${@:-1 28}"; do
  for f2 in $f; do
    d="$ROOT/out/proglogs/probe-$P/f$f2"; mkdir -p "$d"
    CW_PROGRAMS_LOG_DIR="$d" CW_RENDER_SCALE=0.5 timeout 300 "$ROOT/games/adventure/tools/swshot.sh" adventure_bench 200 /dev/null ADV_QUALITY=3 ADV_BENCH=1 ADV_FROM=$f2 "ADV_PROBE_$P=1" >/dev/null 2>&1 || true
  done
done
cat "$ROOT"/out/proglogs/probe-"$P"/*/adventure_bench.log 2>/dev/null | sort -u | wc -l
