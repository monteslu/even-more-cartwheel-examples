#!/usr/bin/env bash
# build-cast.sh [name ...]: the cast from the CC0 bases. Blender (headless, CPU only) builds each figure
# (blender/build_chars.py: pose, size, dress, recolour, bake the skin atlas) into out/, then make-characters.mjs writes
# its glb with the game's skeleton and clips into assets/models/. Needs flatpak Blender (org.blender.Blender, 5.x).
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
flatpak run --filesystem=home org.blender.Blender --background --factory-startup \
  --python "$HERE/blender/build_chars.py" -- "$HERE/cc0" "$HERE/out" "$@" 2>&1 | grep -E '^\[chars\]|Error|Traceback|line [0-9]+' || true
grep -q . "$HERE/out/cc0/"*.json
node "$HERE/make-characters.mjs" "$@"
