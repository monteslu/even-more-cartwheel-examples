# lanternfall: handoff (internal working notes for the next session on branch lanternfall)

## State (2026-11-07, second round)
- Branch `lanternfall`, worktree `worktrees/lanternfall`. Engine: `deps/cartwheel-engine` is a symlink to
  `/home/monteslu/code/cliemu/cartwheel-engine-wt/lanternfall-v037`, a detached worktree of cartwheel-engine at the
  v0.37.0 tag (9280c42), its deps fetched at the engine's pins by `scripts/fetch-deps.sh` (CARTWHEEL_ENGINE set to that
  path). The old `lanternfall-pin` worktree (24b12f5) is no longer used; removing it was refused by the permission
  check, so it is still on disk: `git -C /home/monteslu/code/cliemu/cartwheel-engine worktree remove --force
  /home/monteslu/code/cliemu/cartwheel-engine-wt/lanternfall-pin` when allowed. `scripts/deps.lock` is not ours to move
  (the coordinator moves it once the v0.37.0 SDK assets are published). The pinned SDK deps are parked untracked in
  `deps-sdk/`. The game picks the new 2D renderer by `CW_R2D_FEATURES >= 2` (src/view.h).
- Builds: `CW_THREE_DIR=/home/monteslu/code/cliemu/cartwheel-engine-wt/lanternfall-v037/deps/three.c TARGETS=native
  scripts/build.sh lanternfall lanternfall_auto lanternfall_bench lanternfall_soak lanternfall_check lanternfall_feel lanternfall_worldgen`
  (the build tree had cached an older three.c; the variable forces the right one). Binaries land in
  `build/native/project/games/lanternfall/`. Carts: the same with `TARGETS=cart-wgpu`; they land in `build/cart-wgpu/carts/`.
  `out/b.sh <apps>` wraps this (development helper, not shipped).
- World: rebuild with `lanternfall_worldgen games/lanternfall/assets/world.bin`, then `lanternfall_check
  games/lanternfall/assets/world.bin --threads 24 --route games/lanternfall/assets/route.bin`. `CHECK_STUCK=1` on the
  checker prints the doors a stage reached one side of and not the other (for finding where new terrain blocks the
  way). `lanternfall_worldgen /dev/null --list` (rooms), `--room n` (tiles), `--pickups` (pickups, shrines, wardens).

## This round
1. Composition (src/worldgen.c `compose`, the platform loop, the columns, `populate`): rolling ceiling line,
   hanging masses, wall bulges, islands with tapered undersides, broken ledges, broken columns with stumps, flooded
   halls, region set pieces (PR_MACHINE, PR_TREE, PR_CLUSTER, PR_KELP, PR_SHELF, PR_STATUE, PR_PIPE in world.h),
   pilot lights (lamp style 8) and lantern posts (style 9). No slopes (player.c untouched). Tile corners open on two
   sides are rounded in the art. Framing silhouettes in their own layer (view.c `draw_framing`, `frame_at`: a
   translation so a group sits at its world place when the camera is at its edge and slides 1.32x as fast).
2. Background: parallax families in src/art_par.h (ridges, arches, trees, crystals, machinery, hanging growth),
   three depths per region (view.c `draw_sky`, SET table), all in the sky layer with the haze baked in. The back wall
   is a rough cave rock (no courses), only in a band round the rock, mapped across eight tiles.
3. Colour: values by luma (`toluma`, the V_* steps, a per-region EXPO nudge), complementary accents
   (`comp_accent`). tests/lanternfall-look.mjs captures eight regions on lavapipe and measures them (see DESIGN.md).
4. Bot: full clear (see DESIGN.md, the bot). Route: the heart last (check.c write_route). Sim: a warden wakes only
   once its player is clear of a door in the arena's ceiling (the lock had shut on a player still in the hole).
5. Perf: parallax merged into the sky layer (four fewer layers), back wall skipped behind rock and kept to a band,
   no full-screen quads in the lit layers.

## Round two finish (2026-11-07)
- WebGPU exited on start-up: on the first frame the canvas size is 0 x 0 (no pass yet), the zoom became 0, the view
  NaN, and every entity in the world was drawn in one frame (604,752 vertices); Dawn aborted in the index upload.
  `view_canvas` (view.c) falls back to the window size and draws nothing while that is 0 too. Details and the Dawn
  side: internal-cartwheel findings/lanternfall-2d.md.
- GI field at a quarter of the canvas (was an eighth): finer and, on v0.37, cheaper (bench GL 4.2 -> 3.4 ms,
  WebGPU 4.6 -> 3.8 ms). Engine v0.38.0 cuts GI to about 0.3 ms; the pin move is the coordinator's.
- Soak: 24 runs (12 starts, one and two players) all clear; the stall at route step 3672 in room 321 (out/tr.txt
  of the previous session) does not happen on this world and bot. The soak test runs two starts per player count.

## Look round (2026-11-07)
- Renamed from explorer to Lanternfall (apps, carts, tests, art).
- Rock: one atlas per region (src/art.c `rock`, `make_rock`: 16 edge variants for each of the 4 places in the body's
  two-tile period), drawn as batched quads with per-corner shade (view.c `draw_tiles`, `corner_colour`).
- Undersides shaped past the tile staircase (view.c: wedges from tip to tip, fillets in leftover concave corners;
  `LANTERNFALL_PERF_OFF=shape` turns it off). Drops through floors in their own unlit layer (view.c `draw_drops`).
- GI field back to an eighth: on engine v0.38 it is the cheaper share (bench GL 3.16-3.27 -> 2.96-3.09 ms, WebGPU
  3.72-3.77 -> 3.12-3.19 ms).

## Fun round (2026-11-07, branch lanternfall-fun)
- Combat and movement round: hit-stop (sim `hitstop`, the bot and the soak skip held ticks), knockback (`enemy.kb`),
  recoil (`player.recoil_t`), four new kinds (EN_PUFF, EN_DROPPER, EN_BURROWER, EN_EYE; drawing in view.c
  `draw_enemy_extra`), region casts (worldgen.c `CAST`, which keeps the generator's draw count so layouts hold),
  trials (sim.c `trial_update`), charms (`CH_*`, relics in order), warden stagger and second phase, creatures
  staying down until a rest (`sim.slain`), the flame left where you fall (`sim.echo`), a dash off a wall
  (player.c, and two checker moves appended at the end of the move list).
- Save version 2 (the save grew charms, trials, the flame).
- `LANTERNFALL_SOAK_TIMELINE=1` prints the moments a player notices by game second; `LANTERNFALL_SOAK_FAR=1` widens
  the trace's enemy list. The soak's FAIL line names the trial and lists the enemies left.
- `g_rules_off` (sim.h RULE_*) turns one rule off for tools/rules.c's controls.

## Debugging switches
- Fast looks without the GPU lock: `node tests/lanternfall-look.mjs` (lavapipe, out/look-lanternfall/r<region>.png), or
  run lanternfall_auto on lavapipe by hand (SDL_VIDEODRIVER=offscreen, VK_ICD_FILENAMES=lvp, CW_GPU=webgpu,
  CW_SCREENSHOT, LANTERNFALL_IDLE=1 LANTERNFALL_ROOM=n LANTERNFALL_NO_HUD=1).
- Perf breakdown: `LANTERNFALL_PERF_OFF=shadows,vol,post,water,dist,occl,lights,frame`, `LANTERNFALL_NO_GI=1`, and
  `LANTERNFALL_GI_RES=<share>` (the GI field's share of the canvas; 0.125 ships). Numbers per part: internal-cartwheel
  findings/lanternfall-2d.md, "Perf on v0.37.0".
- Bot: `LANTERNFALL_SOAK_HURT=1 lanternfall_soak games/lanternfall/assets <min> 1` prints hurts, deaths, REGRESS lines;
  `LANTERNFALL_SOAK_SEED=n` plays another run (the bot starts n % 97 ticks late); `LANTERNFALL_SOAK_FROM=a LANTERNFALL_SOAK_TRACE=b` traces ticks a..b (with the warden's state when one is up). A full
  clear takes about 140 game minutes, about 3 s of CPU.

## Open
- Local contrast of the frames is 5 to 10 (clean shapes in haze); the look check's floor is 4.5.
- Push: done once at the end of each round; a refused push is not retried.
