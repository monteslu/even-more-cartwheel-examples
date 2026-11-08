# Destructo

Bring structures down. Towers, walls, bridges, castles, scaffolds and stepped pyramids of wood, stone, glass and steel,
hundreds to a few thousand blocks each, stand on a field. You have a launcher, a few rounds of shot, a heavy ball or
two and some charges. Every structure hides glowing crystals: bring them all down to clear it. Red powder kegs stand
against the foot of some structures: a keg that is hit goes off, and its blast sets off the next.

One to four players take turns at the one launcher on the one structure: together (co-op: one count of shots against
par), against each other (versus: each shot scores for whoever fired it), or at a party (steady hands: ammunition
without end, each turn must bring at least one block down, a turn that brings none is a strike and two strikes put you
out, and whoever brings a crystal down is out; the last one in wins).

## Playing

| Control | Does |
|---|---|
| d-pad or left stick | turn and raise the launcher (a tap moves it a hair, held it speeds up) |
| shoulders (triggers too) | power |
| north | the ammunition: shot, heavy ball, charge |
| west, held | the wide view of the structure |
| south | fire |

On the result card, north watches the last shot again and any button skips the replay. On the select screen west
turns on free fire: every structure open, ammunition without end, nothing judged.

### Results and stars

A level is judged once its world is still (at most two bodies awake for a third of a second; a turn's settle ends
after 15 s whatever still moves), never while the structure is falling. The share of the structure brought down is
counted by volume over every block the level placed (crystals not counted): a block is down when it is broken, gone
off the field, or more than its own size (and at least 0.6 m) from where it settled before play. Each kit has a mark:
half a tower or a scaffold, two fifths of a keep, about a third of a wall, a quarter of a castle or a bridge, 15% of a
stepped pyramid. The HUD shows the share against the mark while you play.

| Stars | Needs |
|---|---|
| one | every crystal down |
| two | ... within par |
| three | ... and the structure brought down to its mark |

The card says BROUGHT DOWN when the mark is reached and CRYSTALS DOWN when the crystals fell with the structure
mostly standing. It slides in at the right edge, clear of the pile.

### Instant replay

The simulation is deterministic, so the replay needs no saved world: a second game is made the way the level was and
stepped with the inputs the players gave (only the ticks something was held are logged). While the players aim it
catches up a few milliseconds a frame, while the live world is quiet; while a shot flies it waits on the tick before
that shot was fired. When the level is judged it therefore stands where the last shot started. The replay follows the
shot, turns slowly round the impact in slow motion for the first second and a quarter after it, and ends on exactly
the state the live game was judged on (every block's pose to the bit, the counters and the scores).

The aim path shows the first part of the shot's flight, as the world will fly it. Charges go off on the first thing
they touch and push and break everything within about four and a half metres; shot that breaks what it hits keeps the
speed the break did not use and goes on through.

## How it is made

- `src/sim.c`: the simulation. Blocks are bodies in the engine's Box3D world (`cartwheel/physics3d.h`). Box3D does not
  fracture, so the game does: every block shape reports hit events, a hit's energy (the pair's reduced mass times the
  approach speed squared over two) comes off the block's strength (its material's toughness times its volume), and a
  block out of strength becomes two or four pieces that tile it (moving as that part of it moved), or dust and shards
  when it is glass, small or already a piece. Past 3000 bodies a break leaves dust only; past 1500 awake bodies stone
  splits in two, not four. Breaks wait in a fracture queue (24 a tick) and a charge's blast runs outward over four
  ticks, a shell a tick: a blast that broke hundreds of blocks in one tick was a dropped frame in the cart.
- `src/levels.c`: thirty levels from seven kits. Walls are running-bond brick courses that hand the corners to the
  other pair of walls each course, so they lock; floors are planks that the storey above stands on. Each kit has a
  character: stepped roofs in red, slate or copper with gilded finials (a wide step is a ring of four boards, a third
  the weight of a solid slab; a six-storey tower's roof stands on its top floor, since every course more on it is a
  lean that creeps), battlements, trim courses, timber-framed top storeys, a castle gate under a steel lintel, brick piers with cream caps, a turquoise-banded pyramid with a gilded
  shrine; banners on poles ride the blocks they stand on; windows are glass bricks lit from inside.
- `src/replay.c`: the instant replay (above).
- `src/bot.c`: the bot plays through the controls: it flies the shot's path the way the world will (gravity in the
  world's sub-steps), picks a straight shot at a crystal it can see, a lob over what hides it, a charge, or a heavy
  ball into what holds it up, then holds and taps the aim buttons until the launcher points there.
- `src/view.c`, `src/trees.c`, `src/fx.c`, `src/sound.c`: one instanced box per material with a texture made at
  start-up and the builder's paints in real material colours (warm stone, timber, plaster, slate, terracotta,
  copper), the roofs' boards drawn with a course texture of their own; a setting per kit (highlands under
  snow-capped mountains, a golden meadow, a river gorge, a headland over the sea, a quarry, red mesas over pale
  dunes, a snowy pass) with its own ground (rolling past the field of play, its colour wandering, the yard and the
  track worn into it), backdrop, light, haze and share of a contrast grade (`assets/grade/contrast.cube`, from
  `tools/gen-grade.mjs`); trees of layered foliage (alpha-cut leaf, needle and frond cards made at start-up, their
  normals out from each clump so a crown shades like a volume): oaks, birches, spruces, snow-laden firs and date
  palms, two species a setting, swaying in the wind; a column of light over every standing crystal; dust, chips, shards, sparks and the engine's explosion kit, all fired once
  under the ground at load so no effect compiles its programs mid-game; a plume of dust that hangs over a collapse,
  drifts downwind and thins over about ten seconds; the ground marked where the structure came down; broken pieces
  showing their raw inside; grass on green ground round the yard; the sea under the headland as the engine's water;
  sounds made in code.
- No strobing. Every light swells over a tenth of a second or more and eases out, never steps or flickers; the
  explosion kit's own flash light is off (it starts at its peak and flickers). No screen shake: the pad rumbles.

## What Box3D can do here (tools/physcheck.c, tests/destructo-physics.mjs)

- **Stacks at rest.** With sleeping on (the game's setting) a brick tower, a 30-block column, a 385-block pyramid and
  a plank scaffold move nothing at all after settling, and all sleep within 2 s. With sleeping off the solver's own
  creep shows: the 30-block column wanders 72 mm in a minute, the scaffold 2.6 mm, the tower and pyramid 0.1 mm. Two
  things make a stack creep for ever and the levels avoid both: a rigid plank or beam on three supports teeters on the
  middle one (soft contacts let the supports sink by different amounts), so long floors and beams are split over the
  supports; and a tall frame of loose posts creeps, so scaffolds grow wide, not tall.
- **Joints do not hold a frame.** Bolting a steel frame's posts and beams with rigid weld joints oscillated the frame
  apart within a second (3 or more storeys); soft welds sag. The game uses no joints.
- **Budget.** All-awake colliding blocks on the job pool (24 threads here): 1000 bodies about 1.3 ms a step, 2000
  about 2 to 3, 3000 about 2 to 5 depending on the load of the machine; one thread is 4 to 7 times slower.
- **Determinism.** One collapse hashes the same with 1, 2, 4 and every worker; the play gate gets the same bits
  natively and in the cart on romdev, on GL and on WebGPU.
- **Tunnelling.** At 60 Hz with 4 sub-steps a fast shot passes through thin dynamic blocks (a 2 cm pane, a 6 cm plank)
  at 90 m/s and up unless the shot is a `bullet`; more sub-steps do not help. Static walls always stop it (continuous
  collision; with it off the control goes through). The game's shot is a bullet.

## Performance

On the Radeon 890M at 1920x1080 on High, during Great Keep's collapse (1241 blocks broken, 2182 bodies awake and
drawn): GPU 3.56 ms on GL and 3.41 ms on WebGPU (`tests/destructo-perf.mjs`); without the trees
(`DESTRUCTO_NO_TREES=1`) the same collapse measured 3.3 ms and 3.0 ms. The trees cast and take no shadow (they stand
past the sun's shadow map) and the ground has a hole under the yard, which is drawn once. SSAO is off (0.9 ms, the
edges of the block textures carry the shading instead) and the picture is anti-aliased with SMAA rather than 4x MSAA (WebGPU
multisamples 4x for any MSAA setting; SMAA halved its scene pass). Physics in the cart (romdev's host) during the same
collapse: about 2.3 ms a step at worst over the collapse windows.

- **The blast frame.** A charge on Great Keep breaks about 800 blocks. Before the fix the frame it went off in took
  33 ms in the cart and 70 ms natively, and nearly all of that was not physics: the first explosion, dust and shards
  compiled their GPU programs on that frame. Three things fix it: every effect is fired once under the ground at
  load, so its programs are built before play; breaks wait in a fracture queue and at most 24 run a tick; and a
  charge's push runs outward over four ticks, a shell of the blast a tick. The longest frame of the blast is now
  13.2 ms in the cart on romdev and 10.3 ms natively, against 16.6 ms (`tests/destructo-hitch.mjs`; its control,
  `destructo_bench_nobudget`, is the game as before the fix and must go over).

- **Memory.** Making, stepping and destroying worlds gives the memory back; natively the job threads' own glibc
  malloc arenas hold fragments (a few MB over hundreds of worlds), which one arena (`MALLOC_ARENA_MAX=1`) removes.
  The soak runs that way and checks the last lap of levels holds no more than the lap before.

## Tests

| Command | Checks |
|---|---|
| `node tests/destructo-physics.mjs` | the measurements above, each with a control that must fail |
| `node tests/destructo-levels.mjs` | every level stands still for a minute, nothing floats or is placed inside another, the bot clears it within par; the level is judged only once still and its stars and headline match the structure 5 s later; the replay ends on the live state; each kit's mark is reachable; two kegs chain (controls: the old result rules, a nudged replay, kegs too far apart) |
| `node tests/destructo-soak.mjs [--quick]` | the bot level after level, 1 to 4 players: no NaN, no jitter left awake, no memory growth, every replay on the live state; and free fire on Great Keep |
| `node tests/destructo-result.mjs` | in the game: the result judged with at most 10 bodies awake, the replay ends on the live state (controls: the old rules, a nudged replay) |
| `node tests/destructo-flash.mjs` | every frame of Great Keep's blast through the engine's flash check (WCAG 2.3.1's general and red flash thresholds, small lights switching together, regular flicker) (controls: the game's 10 Hz wash and the engine's 8 Hz strobe) |
| `node tests/destructo-perf.mjs` | the GPU frame at 1920x1080 on High during a castle's collapse, GL and WebGPU, on the 890M |
| `node tests/destructo-hitch.mjs` | the longest frame of Great Keep's blast under 16.6 ms, natively and in the cart on romdev |
| `node tests/destructo-look.mjs` | one picture of each setting: mean luma 95 to 125 and a deviation of 55 or more (never flat, never washed out) |
| `node tests/gate.mjs --gates-file destructo-gates.json` | a person's first shot: the same collapse on every host and both GPU paths |
