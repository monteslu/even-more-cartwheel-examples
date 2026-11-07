# Changelog

## 0.1.0

The first release: two games on engine v0.34.3, each as one dual GL + WebGPU cart.

- **towers** (working title): a tower defense game for one to four players on one screen. Each player builds
  in their own share of the map; the four towers (chain lightning, frost, a ramping beam, a mortar) build from
  a diamond of the face buttons round the cursor and upgrade to level 3. Waves of eight creep kinds grow into
  the thousands on the crowd system, with a boss every tenth wave. 30 maps in six themes (meadow, autumn,
  desert, snow, volcano, crystal) with six path layouts, merging gates, rock and pools, each baked and checked
  in CI: the path, the pads, fair shares, grounding, the bake and a two-bot clear.
- **pinball** (working title): three original tables (Coral Deep, Star Foundry, Neon Fair) with ramps, pop
  bumpers, drop targets, four modes each and a three-ball multiball, for one to four players taking turns.
  The ball runs on the game's own solver, swept and substepped so it never passes through a wall or a flipper
  at any speed. A dot display, a chrome ball with reflections, lit inserts and flashers, nudge and tilt, and a
  saved top five per table.
