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
  seed: winding paths two tiles wide from the gates on the edge to the core, pads beside them dealt into fair shares
  for two, three and four players, the theme's decor and lamps (the format also takes rock and water tiles).
- `tools/mapview` (`towers_mapview`) builds a map as the game does (`mapbuild.c`) and bakes its lighting:
  `scripts/build.sh towers_mapview`, then `towers_mapview --bake games/towers/assets/lightmaps` (through the GPU
  suite lock) bakes all 30 and writes each bake's hash beside it. As a cart it shows the maps for review.
- `tools/check-maps.mjs` (CI) fails a map whose gates cannot reach the core, whose pads are off the path or touch,
  whose shares are unfair, whose props float or sink, whose bake is missing or stale, or that two bots cannot hold
  (`--soak build/native/games/towers/towers_soak`). The maps in `tools/maps-broken/` must each fail their own check.
- `tools/gen-textures.mjs`, `tools/gen-luts.mjs` and `tools/gen-roots.mjs` write the ground textures, the colour
  grades and the pack list. The towers, pads, core, gates, lamps and decor are modelled by
  `tools/blender/towerkit.py` in headless Blender.

## Building and testing

`scripts/fetch-deps.sh`, then `scripts/build.sh towers towers_auto`. `towers_auto` is the game played by bots;
`towers_bench` is a late map with towers on every pad and 2400 creeps on its paths (the perf gate's scene).

- `node tests/gate.mjs --gates-file towers-gates.json`: the bots' match and the build diamond, every host agreeing.
- `node tests/towers-perf.mjs`: the perf gate (GPU time a frame at 1920x1080 High on GL and WebGPU, with controls).
- `node tests/towers-soak.mjs`: 30 game minutes of bots at one to four players with checks every tick.
- `towers_soak --clear <map.cwtd>`: two bots play one map (the map check's clear run).
- Development switches (native): `TOWERS_MAP=n` starts on map n, `TOWERS_PLAYERS=n` the auto build's players,
  `TOWERS_WAVE=n` starts at wave n with towers on every pad, `TOWERS_CROWD=n` keeps n creeps on the paths,
  `TOWERS_BENCH=1` prints timings, `TOWERS_QUALITY=0..4` a graphics preset, `TOWERS_NO_HUD=1` and
  `TOWERS_NO_CURSORS=1` (auto builds) clean frames for key art. The auto cart answers the debug command `map <n>`.
