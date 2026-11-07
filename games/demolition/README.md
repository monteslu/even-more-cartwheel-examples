# demolition (working title)

Bring structures down. Towers, walls, bridges, castles, scaffolds and stepped pyramids of wood, stone, glass and steel,
hundreds to a few thousand blocks each, stand on a field. You have a launcher, a few rounds of shot, a heavy ball or
two and some charges. Every structure hides glowing crystals: bring them all down within par for three stars.

One to four players take turns at the one launcher on the one structure: together (co-op: one count of shots against
par) or against each other (versus: each shot scores for whoever fired it).

## Playing

| Control | Does |
|---|---|
| d-pad or left stick | turn and raise the launcher (a tap moves it a hair, held it speeds up) |
| shoulders (triggers too) | power |
| north | the ammunition: shot, heavy ball, charge |
| west, held | the wide view of the structure |
| south | fire |

The aim path shows the first part of the shot's flight, as the world will fly it. Charges go off on the first thing
they touch and push and break everything within about four and a half metres; shot that breaks what it hits keeps the
speed the break did not use and goes on through.

## How it is made

- `src/sim.c`: the simulation. Blocks are bodies in the engine's Box3D world (`cartwheel/physics3d.h`). Box3D does not
  fracture, so the game does: every block shape reports hit events, a hit's energy (the pair's reduced mass times the
  approach speed squared over two) comes off the block's strength (its material's toughness times its volume), and a
  block out of strength becomes two or four pieces that tile it (moving as that part of it moved), or dust and shards
  when it is glass, small or already a piece. Past 3000 bodies a break leaves dust only; past 1500 awake bodies stone
  splits in two, not four.
- `src/levels.c`: thirty levels from seven kits. Walls are running-bond brick courses that hand the corners to the
  other pair of walls each course, so they lock; floors are planks that the storey above stands on.
- `src/bot.c`: the bot plays through the controls: it flies the shot's path the way the world will (gravity in the
  world's sub-steps), picks a straight shot at a crystal it can see, a lob over what hides it, a charge, or a heavy
  ball into what holds it up, then holds and taps the aim buttons until the launcher points there.
- `src/view.c`, `src/fx.c`, `src/sound.c`: one instanced box per material with a texture made at start-up, the field
  and its hills and trees, the launcher; dust, chips, shards, sparks and the engine's explosion kit; sounds made in code.

## What Box3D can do here (tools/physcheck.c, tests/demolition-physics.mjs)

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

On the Radeon 890M at 1920x1080 on High, during Great Keep's collapse (1444 blocks broken, 2374 bodies awake, 2846
blocks drawn): GPU 3.2 ms on GL and 2.9 ms on WebGPU (`tests/demolition-perf.mjs`). SSAO is off (0.9 ms, the edges
of the block textures carry the shading instead) and the picture is anti-aliased with SMAA rather than 4x MSAA
(WebGPU multisamples 4x for any MSAA setting; SMAA halved its scene pass). Physics in the cart (romdev's host) during
the same collapse: 2.9 ms a step on average while more than 500 bodies are awake, against 2.4 ms natively; the
worst single step is the blast itself (about 17 ms in the cart, 7 natively), when hundreds of pieces are made and a
thousand bodies wake at once.

- **Memory.** Making, stepping and destroying worlds gives the memory back; natively the job threads' own glibc
  malloc arenas hold fragments (a few MB over hundreds of worlds), which one arena (`MALLOC_ARENA_MAX=1`) removes.
  The soak runs that way and checks the last lap of levels holds no more than the lap before.

## Tests

| Command | Checks |
|---|---|
| `node tests/demolition-physics.mjs` | the measurements above, each with a control that must fail |
| `node tests/demolition-levels.mjs` | every level stands still for a minute, nothing floats or is placed inside another, the bot clears it within par |
| `node tests/demolition-soak.mjs [--quick]` | the bot level after level, 1 to 4 players: no NaN, no jitter left awake, no memory growth |
| `node tests/demolition-perf.mjs` | the GPU frame at 1920x1080 on High during a castle's collapse, GL and WebGPU, on the 890M |
| `node tests/gate.mjs --gates-file demolition-gates.json` | a person's first shot: the same collapse on every host and both GPU paths |
