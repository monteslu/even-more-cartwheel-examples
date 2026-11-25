#!/usr/bin/env bash
# story.sh: Ember Throne's story (story/ember.story) compiled to assets/story/ember.cwstory and voiced into
# assets/story/ember.cwspeech with the engine's tools (deps/cartwheel-engine/tools: story-compile.mjs, speech-bake.mjs).
# The voices are Kokoro-82M run from node by kokoro-js on the CPU (no Python): install it once,
#   npm install --prefix ~/tools/kokoro kokoro-js@1.2.1
# and point KOKORO_JS_MODULES at its node_modules. Takes are cached in out/emberthrone-voice, so a rerun speaks only the
# lines that changed. --check: compile only and fail when the committed story is not the script's (CI).
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
ENGINE="${CW_ENGINE_ROOT:-$ROOT/deps/cartwheel-engine}"
mkdir -p "$ROOT/out/emberthrone-voice"
if [ "${1:-}" = "--check" ]; then
  exec node "$ENGINE/tools/story-compile.mjs" "$HERE/story/ember.story" -o "$HERE/assets/story/ember.cwstory" --check
fi
node "$ENGINE/tools/story-compile.mjs" "$HERE/story/ember.story" -o "$HERE/assets/story/ember.cwstory" --lines "$ROOT/out/emberthrone-voice/lines.json"
node "$ENGINE/tools/speech-bake.mjs" "$ROOT/out/emberthrone-voice/lines.json" -o "$HERE/assets/story/ember.cwspeech" \
  --kokoro "${KOKORO_JS_MODULES:?set KOKORO_JS_MODULES to kokoro-js node_modules}" --cache "$ROOT/out/emberthrone-voice/cache" ${ASR:+--asr}
