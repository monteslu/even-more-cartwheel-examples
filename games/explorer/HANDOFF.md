# explorer: handoff (internal working notes for the next session on branch explorer)

## State (2026-11-07)
- Branch `explorer`, worktree `worktrees/explorer`. Engine: `deps/cartwheel-engine` is a symlink to
  `/home/monteslu/code/cliemu/cartwheel-engine-wt/explorer-pin` (a detached checkout of engine-2d at 24b12f5, its deps
  symlinked to engine-2d's). The pinned SDK deps are parked untracked in `deps-sdk/` (hardlinks). The game picks the new
  2D renderer by `CW_R2D_FEATURES >= 2` (src/view.h), else draws the old way (CI builds against the v0.34.3 SDK).
- Builds: `TARGETS=native scripts/build.sh explorer explorer_auto explorer_bench explorer_soak explorer_check
  explorer_feel explorer_worldgen`; binaries land in `build/native/project/games/explorer/` (engine-source layout).
  Carts: `TARGETS=cart-wgpu scripts/build.sh explorer explorer_auto explorer_bench`.
- Passing: tests/explorer-world.mjs (world current + proven, five controls), tests/explorer-feel.mjs (three controls),
  tests/explorer-soak.mjs (60 min, floors 8 pickups 2 wardens), GPU gates explorer-auto / explorer-play on GL and
  WebGPU incl. romdev, cart-smoke, cart-art, check-prompts.
- Perf (tests/explorer-perf.mjs, 890M 1080p, bench arena): GL 3.62 ms, WebGPU 4.11 ms. Breakdown: lit HDR floor 1.5 ms,
  post ~1.1, GI ~1.2 (now at 1/8 resolution), soft shadows ~0.45. `EXPLORER_PERF_OFF=shadows,vol,post,water,dist,occl,lights`
  and `EXPLORER_NO_GI=1` switch parts off for a sweep.
- Fast looks without the GPU lock: run explorer_auto on lavapipe (SDL offscreen, VK_ICD_FILENAMES=lvp, CW_GPU=webgpu,
  CW_SCREENSHOT, EXPLORER_IDLE=1 EXPLORER_ROOM=n). `explorer_worldgen /dev/null --list` lists rooms (index region kind
  size); `--room n` prints a room's tiles.
- Bot debugging: `EXPLORER_SOAK_HURT=1 explorer_soak assets <min> 1` prints hurts, deaths and REGRESS lines;
  `EXPLORER_SOAK_FROM=a EXPLORER_SOAK_TRACE=b` traces ticks a..b. A full soak minute takes about 0.05 s of CPU.

## The coordinator's next round (not done yet)
1. Room composition: varied terrain (slopes need collision support in player.c; overhangs, rounded masses, pillars,
   broken ledges, alcoves in worldgen carve_room), foreground framing silhouettes drawn in front (a layer after L_FX),
   set pieces per region (forge machinery and molten channels, flooded hall, garden). Every terrain change must keep
   explorer_check passing (rerun tests/explorer-world.mjs; regenerate world.bin and route.bin).
2. Background: replace the uniform brick back wall (art.c make_wall, view.c draw_room_wall) with region shapes
   (machinery, arches, trees, crystals) with depth haze; vary the parallax silhouettes (art.c make_misc par[]).
3. Colour: keep each region's key hue, add a complementary accent (warm lamps in cool regions are started in
   draw_lamp; cool steel and blue pilot lights for the Forge); keep the value step. Add a look check like towers':
   mean luma 100..120 (except deliberately dark regions), sd >= 55, with a control.
4. Bot must finish the game: the loop is at route step ~1963 (room 142, Glasswater: a moth swoop knocks it into thorns,
   then it climbs back from the safe spot). Ideas: kill moths first (shoot up when under, jump-strike), wait out moth
   tells before moves, prefer moves whose landing is away from thorns, retreat and re-approach. Then raise the soak's
   floors to a full clear (won 1).
5. Perf: merge parallax layers that share depth, draw the back wall only where it is not covered by rock (it already
   fades by rock distance), keep full-screen quads out of lit layers; report GL and WebGPU.
6. Push once at the end (the second push this session was refused by the permission check; do not work around it).
