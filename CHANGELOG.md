# Changelog

## 0.3.0

- **explorer** (working title): a 2D exploration platformer for one player with a drop-in second. One
  underground world of 13 regions and 356 rooms; seven abilities (a second jump, a dash, a wall climb, a plunge,
  swimming, a grapple and a spark) each open ways that were shut. Six wardens, shrines that save and carry you
  between them, and a map that fills in as you explore. Drawn with the engine's 2D renderer: lit sprites, soft
  shadows, bounced light, light shafts, bloom and a colour grade per region. CI checks that the world can be
  finished in ability order with no dead ends, every pickup reached and nothing floating, and that the jump's
  feel stays in its bounds; a bot plays it through to the end.

## 0.2.0

- **skate** (working title): skateboarding for one to four players in split screen, in five original parks,
  each with its own setting to the horizon and its own time of day, lightmapped. Flips, grabs, spins, grinds and
  manuals with balance meters link into combos; a bail loses the combo and drops the rider as a ragdoll. Free
  skate, a two-minute score attack with goals and a saved high score per park, and score battles. The board runs
  on the game's own controller, substepped and swept against the park's triangles; every park is checked in CI
  (nothing floats, the bakes are current, a bot skates every line).

## 0.1.1

- Engine v0.38.0 (its 2D rendering releases v0.37.0 and v0.38.0: tiled 2D lights, soft shadows, a high-range
  2D post chain, 2D bounced light, water, GPU particles, sprite instances, colour grading). towers and pinball
  draw the same as before.

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
