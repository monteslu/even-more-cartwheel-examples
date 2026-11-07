#!/usr/bin/env bash
# swshot.sh <app> <frames> <out.png> [ENV=V ...]: a screenshot on software GL (llvmpipe), off the shared GPU, for
# development looks at the world and the characters (slow, but it never queues behind the GPU suites)
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
APP="$ROOT/build/native/games/adventure/$1"; FR="$2"; OUT="$3"; shift 3
exec env LIBGL_ALWAYS_SOFTWARE=1 LP_NUM_THREADS=4 CW_WORKERS=3 SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy CW_GPU=gl CW_FRAMES="$FR" CW_WARMUP_SPREAD=0 \
  CW_SCREENSHOT="$OUT" "$@" nice -n 5 "$APP"
