# Towers (working title)

Tower defense for one to four players on one machine. Every player has a cursor and builds in their own share of the
map while one shared wave after another comes down the paths for the core, late waves thousands strong. Four towers,
three levels each: chain lightning, freeze fields, beams and mortars. 30 maps in six themes. The design is in
[DESIGN.md](DESIGN.md).

## Playing

| | Pad | Keyboard (player 1) |
|---|---|---|
| hop between your pads | d-pad or left stick | W A S D or the arrows |
| build on an empty pad | the four face buttons, as a diamond: north bolt, west frost, east beam, south mortar | I J L K |
| on your tower: upgrade / aim / sell (hold) | south / north / west | K or Space / I / J |
| call the next wave early | a shoulder | Q |
| pause | Start | Escape |

The build diamond round your cursor shows which button builds what and its price. Every kill pays the whole team
alike, and every wave pays everyone; calling a wave early pays a gold for every second left on its clock. A creep that
reaches the core costs it health; hold the core through the last wave to take the map.

## How the maps are made

- Each map is a `.cwtd` text file in `assets/maps/` (the format is in `map.h`), written by `tools/gen-maps.mjs` from a
  seed: smooth winding paths in six families of layout (switchbacks, columns, a spiral, a loop past the core, an open
  field with a choke, a wandering way), more gates merging in on the later maps, pads beside the paths dealt into fair
  shares for two, three and four players, rock outcrops and pools, the theme's decor and lamps. Each path's curve goes into the map too (`route`), and the
  ground is built from it (`map.c`, the ground's shape; `tools/fields.mjs` is the same in JS): the floor, its curb,
  the plazas, the outcrops and the pools all meet in smooth curves, and the creeps walk the floor's part of the tiles.
- `tools/tune-maps.mjs <towers_soak>` sets the difficulty of maps 11 to 30 so two bots hold each while losing some of
  the core (none on map 11, 14 of 20 on map 30); the results live in `tools/map-diffs.json` for `gen-maps.mjs`.
  `tools/gen-broken.mjs` writes the map check's controls.
- `tools/mapview` (`towers_mapview`) builds a map as the game does (`mapbuild.c`) and bakes its lighting:
  `scripts/build.sh towers_mapview`, then `towers_mapview --bake games/towers/assets/lightmaps` (through the GPU
  suite lock) bakes all 30 and writes each bake's hash beside it. As a cart it shows the maps for review.
- `tools/check-maps.mjs` (CI) fails a map whose gates cannot reach the core, whose pads are off the path or touch,
  whose shares are unfair, whose props float or sink, whose floor is shut to the creeps, whose bake is missing or stale, or that two bots cannot hold
  (`--soak build/native/games/towers/towers_soak`). The maps in `tools/maps-broken/` must each fail their own check.
- `tools/gen-textures.mjs`, `tools/gen-luts.mjs` and `tools/gen-roots.mjs` write the terrain textures (ground, path floor, plaza, rock), the colour
  grades and the pack list. The towers, pads, core, gates, lamps and decor are modelled by
  `tools/blender/towerkit.py` in headless Blender.

## Building and testing

`scripts/fetch-deps.sh`, then `scripts/build.sh towers towers_auto`. `towers_auto` is the game played by bots;
`towers_bench` is a late map with towers on every pad and 2400 creeps on its paths (the perf gate's scene).

- `node tests/gate.mjs --gates-file towers-gates.json`: the bots' match and the build diamond, every host agreeing.
- `node tests/towers-look.mjs <frames>`: the look check (value, contrast, accent and smooth edges; frames as
  `TOWERS_NO_HUD=1 TOWERS_NO_CURSORS=1 TOWERS_WAVE=14`, masks with `TOWERS_LOOK_MASK=1`).
- `node tests/towers-perf.mjs`: the perf gate (GPU time a frame at 1920x1080 High on GL and WebGPU, with controls).
- `node tests/towers-soak.mjs`: 30 game minutes of bots at one to four players with checks every tick.
- `towers_soak --clear <map.cwtd>`: two bots play one map (the map check's clear run).
- Development switches (native): `TOWERS_MAP=n` starts on map n, `TOWERS_PLAYERS=n` the auto build's players,
  `TOWERS_WAVE=n` starts at wave n with towers on every pad, `TOWERS_CROWD=n` keeps n creeps on the paths,
  `TOWERS_BENCH=1` prints timings, `TOWERS_QUALITY=0..4` a graphics preset, `TOWERS_NO_HUD=1` and
  `TOWERS_NO_CURSORS=1` (auto builds) clean frames for key art, `TOWERS_LOOK_MASK=1` the terrain alone in flat
  region colours (the look check's edge masks). The auto cart answers the debug command `map <n>`.
