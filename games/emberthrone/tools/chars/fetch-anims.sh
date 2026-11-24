#!/usr/bin/env bash
# fetch-anims.sh: the two motion libraries the cast's clips come from (Quaternius, "Universal Animation Library" and
# "Universal Animation Library 2", the free Standard versions, CC0 1.0), downloaded from OpenGameArt and checked, into
# out/ual/; then blender/build_anims.py makes assets/anim/ual.glb from them (the clips the game plays, on the same rig
# as the cast). Needs flatpak Blender (org.blender.Blender, 5.x).
#   https://opengameart.org/content/universal-animation-library
#   https://opengameart.org/content/universal-animation-library-2
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
D="$HERE/out/ual"
mkdir -p "$D"
get() { # url sha256 dir
  local f="$D/$(basename "$1")"
  [ -f "$f" ] || curl -fsSL "$1" -o "$f"
  echo "$2  $f" | sha256sum -c --quiet
  rm -rf "${D:?}/$3"; mkdir -p "$D/$3"
  unzip -q -o "$f" -d "$D/$3"
}
get https://opengameart.org/sites/default/files/universal_animation_librarystandard.zip 18ff1a7215f4852b320203e8aaf02a1578b5c8eef9027fbaedfcedc7b85a3ac2 ual1
get https://opengameart.org/sites/default/files/universal_animation_library_2standard.zip ec0e40d6d78fe9aaad59e322f40865a8675c22f0745e291622e54520391a9217 ual2
FBX="$(find "$D/ual1" -name 'AL_Standard.fbx' | head -1)"
GLB="$(find "$D/ual2" -path '*Unreal-Godot*' -name 'UAL2_Standard.glb' | head -1)"
MAN="$(find "$D/ual2" -path '*Unreal-Godot*' -name 'Mannequin_F.glb' | head -1)"
flatpak run --filesystem=home org.blender.Blender --background --factory-startup \
  --python "$HERE/blender/build_anims.py" -- "$FBX" "$GLB" "$MAN" "$HERE/out/anims.glb" 2>&1 | grep -E '^\[anims\]|Error|Traceback|line [0-9]+' || true
mkdir -p "$HERE/../../assets/anim"
cp "$HERE/out/anims.glb" "$HERE/../../assets/anim/ual.glb"
echo "assets/anim/ual.glb"
