# Destructo

Bring famous buildings of the world down. The level select holds only real landmarks, by difficulty (the first is a small, simple one a newcomer can clear, and the first control prompts show there): forty-one of them, from every continent and age. Among them: the
Great Sphinx, the Needle of Seattle, the Iron Lady of Paris, the Black Tower of Chicago, the Empire and the Crown
Tower of New York, the Harbour Sails of Sydney, the Pyramids of Giza, the Taj Mahal, the Parthenon, Pisa, Tower
Bridge, Angkor Wat, Chichen Itza, the Statue of Liberty, Notre-Dame, the Pantheon,
the Arc de Triomphe, Hagia Sophia, the Blue Mosque, the Stacked Tower of Taipei, the Golden Span of San Francisco, the
Television Tower of Berlin, the Sail Hotel of Dubai a castle of Japan, the Atoms of Brussels, the Gateway of St. Louis, the Brooklyn Bridge, the Colosseum, Stonehenge, Saint Basil's, the Kremlin, Mount Rushmore, the Brandenburg Gate, the Red Tower of Tokyo, the Temple of Heaven and a five-storey pagoda. Each is scaled to the field with its
proportions kept and built of thousands of small bricks of stone, steel, glass and brick that break the way their
material does. Each level shows the landmark's name and where it stands. More landmarks are built and held until they
pass the landmarks gate and a blind look (`LANDMARK_HELD` in src/levels.c).

Bring structures down. Famous landmarks of wood, stone, glass and steel,
hundreds to a few thousand blocks each, stand on a field. You have a launcher, a few rounds of shot, a heavy ball or
two and some charges. Every structure hides glowing crystals: bring them all down to clear it.

One to four players take turns at the one launcher on the one structure: together (co-op: one count of shots against
par), against each other (versus: each shot scores for whoever fired it), or at a party (steady hands: ammunition
without end, each turn must bring at least one block down, a turn that brings none is a strike and two strikes put you
out, and whoever brings a crystal down is out; the last one in wins). Co-op is one team: any pad aims and fires on any
turn, so a seat that joined and walked away never holds the game up. In versus and party a seat that touches nothing
for eight seconds of its turn is away, and any pad can take that turn (the HUD says so).

## Playing

| Control | Does |
|---|---|
| left stick or d-pad | turn and raise the launcher (a small push is slow and fine, a full push quick; a tap of the d-pad is a hair of a step, a hold picks up speed) |
| right stick | move the launcher round the structure on its circle, so any face can be attacked |
| south | fire, at the ammunition's own fixed power (no charging, no power control) |
| west / east | the next / the previous ammunition: shot, heavy ball, charge |
| north | the wide view, on and off |
| start | pause (the layout is on the pause screen) |

In co-op and free fire the aim opens again the moment the shot has struck, while the rubble is still settling: the launcher turns, the ammunition changes and South fires the next shot at once (a South press while a shot is still in the air is kept and fires the moment the aim opens). On the select screen a held direction repeats, quicker the longer it is held, and the choice wraps round.

The game reads only the sticks, the d-pad and the face buttons: shoulders and triggers do nothing. With a mouse or a finger: drag to aim, tap to fire, tap an ammunition icon to pick it. Hints for a control show until it has been used once.

On the result card, west watches the last shot again and any button skips the replay. On the select screen north
turns on free fire: every structure open, ammunition without end, nothing judged.

### Results and stars

A level is judged once its world is still (at most two bodies awake for a third of a second; a turn's settle ends
after 15 s whatever still moves), never while the structure is falling. The share of the structure brought down is
counted by volume over every block the level placed (crystals not counted): a block is down when it is broken, gone
off the field, or more than its own size (and at least 0.6 m) from where it settled before play. Each landmark has a mark of its own, from
a hundredth of the Pisa tower or the Pyramids to over a third of the tall slender towers. The HUD shows the share against the mark while you play.

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

The launcher stands on a circle round the structure and rolls along it (a full turn in about nine seconds, held); its
turn and raise stay as they were, so it still points at the structure's middle from its new place, and the camera, the
aim path and the bots all work from wherever it stands. When a crystal is hidden from where the launcher stands, the
HUD says so and shows the control that moves round.

The aim path shows the shot's flight as the world will fly it, as far as the structure's near face (seen from where the launcher stands): how high it
arrives there shows, where it comes down behind does not. A ball crushes the masonry round where it first strikes (a
shot within about 1.6 m, a heavy ball 2.2 m) and drives what stands behind it back (3.4 m and 4.6 m): a shot breaks a
hole a few bricks across and knocks a small tower's top off, a heavy ball topples a tower. Charges go off on the first
thing they touch and push and break everything within about four and a half metres.

## How it is made

- `src/sim.c`: the simulation. Blocks are bodies in the engine's Box3D world (`cartwheel/physics3d.h`). Box3D does not
  fracture, so the game does: every block shape reports hit events, a hit's energy (the pair's reduced mass times the
  approach speed squared over two) comes off the block's strength (its material's toughness times its volume), and a
  block out of strength becomes two or four pieces that tile it (moving as that part of it moved), or dust and shards
  when it is glass, small or already a piece. Past 3000 bodies a break leaves dust only; past 1500 awake bodies stone
  splits in two, not four. Breaks wait in a fracture queue (24 a tick) and a charge's blast runs outward over four
  ticks, a shell a tick: a blast that broke hundreds of blocks in one tick was a dropped frame in the cart.
- `src/landmarks.c`, `src/lm_data.c`: the landmarks. Most are cells made by `tools/voxelize.mjs` from an openly
  licensed 3D model of the landmark (CREDITS names each; the models are not in this repository, and
  `tools/landmark-models.json` holds the settings) or from a shape drawn in `tools/landmark-procs.mjs` where no
  good open model exists: `node games/destructo/tools/voxelize.mjs --models <dir>` writes `src/lm_data.c`. A solid
  landmark keeps a skin of small bricks over a core of big blocks (the core is not counted in the share brought
  down); steel lattice, statues, shells and domes are welded members instead (each weld goes when either member
  breaks), so a frame stands that masonry could not. The cells are laid as masonry: each course runs its bricks along x or z in turn,
  and a run of cells is split into bricks one to fourteen cells long by a small search that keeps every brick's middle
  over something it stands on, so a brick can reach a cell past the course below (a dome swelling) or bridge a gap (a
  lintel over an arch, a platform between a tower's legs), and a cell nothing can hold is left out. Courses are laid
  all but touching (every course is a contact that gives a few millimetres under load, and a tall tower laid 4 mm a
  course apart dropped a hand span and slammed); slender towers use tall courses for the same reason, masts and
  antennas are single blocks, and a landmark settles 5.5 s before play (a tall tower sways a few centimetres for four or
  five seconds). Two settings are the landmarks' own: a city (a lawn and a paved square, a skyline of towers far out)
  and a harbour (a paved point out in the water, the city across the bay).
- `src/levels.c`: the levels' table: each landmark with its ammunition, par, mark, setting and starting view, easiest first.
- `src/replay.c`: the instant replay (above).
- `src/bot.c`: the bot plays through the controls: it flies the shot's path the way the world will (gravity in the
  world's sub-steps), picks a straight shot at a crystal it can see, a lob over what hides it, a charge, or a heavy
  ball into what holds it up, then holds and taps the aim buttons until the launcher points there. A crystal still
  hidden after a first try sends it round the structure on the launcher's circle to the nearest place it can be seen
  from. It reads each
  crystal's exact place and aims to a milliradian, so it proves a level can be cleared, not that a person can.
- `src/human.c`: the human-like player of the playability gate: it sees only what the screen shows (the crystals and
  their beacons, the aim path, the HUD, where its last shot struck), aims with a person's error (0.5 m across and 0.8 m
  up and down at the structure, mostly a lean it keeps and corrects from where its shots strike), lets go of the d-pad
  a quarter of a second late, and sends its buttons through the game's pad mapping. A crystal whose glow the structure
  hides from where the launcher stands (its beacon still shows where it is) sends it round toward that side, East held
  with the d-pad, until the glow shows.
- `src/view.c`, `src/trees.c`, `src/fx.c`, `src/sound.c`: one instanced box per material with a texture made at
  start-up and the builder's paints in real material colours (warm stone, timber, plaster, slate, terracotta,
  copper), the roofs' boards drawn with a course texture of their own; a setting per region (highlands under
  snow-capped mountains, a golden meadow, a headland over the sea, red mesas over pale
  dunes, a snowy pass, a city, a harbour) with its own ground (rolling past the field of play, its colour wandering, the yard and the
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
- **Budget.** All-awake colliding blocks in the cart on the job pool (24 threads on romdev; Box3D's wasm SIMD solver):
  2000 bodies about 1.8 to 3 ms a step, 4000 about 3.5 to 4, 6000 about 5.4; one thread is 2.5 to 4.5 times slower
  (6000 bodies: 24.5 ms). The step stops scaling past about 12 threads (6000 bodies: 7.2 ms on 8, 5.8 on 12, 5.4 on 16
  and on 24), and on a loaded machine all 24 made it slower (a blast step 6.2 to 7.1 ms on 24 against 4.0 to 4.2 on 12),
  so the game steps its world on at most 12 (`src/sim.c`; the same bits on any count).
- **Determinism.** One collapse hashes the same with 1, 2, 4 and every worker; the play gate gets the same bits in
  the cart on romdev on GL and on WebGPU.
- **Tunnelling.** At 60 Hz with 4 sub-steps a fast shot passes through thin dynamic blocks (a 2 cm pane, a 6 cm plank)
  at 90 m/s and up unless the shot is a `bullet`; more sub-steps do not help. Static walls always stop it (continuous
  collision; with it off the control goes through). The game's shot is a bullet.

## Performance

On the Radeon 890M at 1920x1080 on High, through romdev's frame timing (the whole frame's GPU time, HUD and final copy
included, as the perf gate measures it), during the Taj Mahal's collapse (720 blocks broken, 1470 bodies awake, 2665
blocks drawn): GPU 5.2 ms on GL and 4.6 ms on WebGPU with the lighting below (`tests/destructo-perf.mjs`); the gate's bound is 6 ms, and four
times the pixels (3840x2160) reads 16.3 ms. `tests/destructo-light-perf.mjs` times one landmark of every setting. The trees cast and take no shadow and
the ground has a hole under the yard, which is drawn once. The light: the sun casts two cascaded shadow maps (the second out to
110 m), a hemisphere light gives the shaded faces the sky's blue and the ground's warm bounce, SSAO (0.6 m) darkens the creases
and shows the carved faces, and a film grade (`tools/gen-grade.mjs`) warms the lit side and cools the shade; together about
1 to 1.6 ms of GPU time a frame. The picture is anti-aliased with SMAA rather than 4x MSAA (WebGPU multisamples 4x for any MSAA
setting; SMAA halved its scene pass). Physics in the cart (romdev's host) runs about 5 ms of CPU a frame over the collapse.

- **The blast frame.** A charge on the Taj Mahal breaks about 700 blocks. Before the fix the frame it went off in took
  33 ms in the cart and 70 ms natively, and nearly all of that was not physics: the first explosion, dust and shards
  compiled their GPU programs on that frame. Three things fix it: every effect is fired once under the ground at
  load, so its programs are built before play; breaks wait in a fracture queue and at most 24 run a tick; and a
  charge's push runs outward over four ticks, a shell of the blast a tick. The longest frame of the blast is now
  6.7 ms of CPU on GL and 10.8 ms on WebGPU in the cart on romdev, against 16.6 ms (`tests/destructo-hitch.mjs`; its control,
  `destructo_bench_nobudget`, is the game as before the fix and must go over).

- **Memory.** Making, stepping and destroying worlds gives the memory back: the cart's memory stays flat over 60
  worlds. The soak checks the last lap of levels holds no more than the lap before (in the cart, its linear memory).

## Tests

Every check runs through romdev (the MCP server on 127.0.0.1:7331), as a cart: no game binary runs natively. The
simulation checks (levels, landmarks, the human-like players, the soak, Box3D) run in the check cart
`destructo_check` (`tools/checkcart.c`: each tool in `tools/` and `src/soak.c` compiled in and started over the
debug channel, `check run <tool> ...`, on a job of the cart's thread pool, its JSON lines read back with `check out`);
the picture and frame checks run the game's own carts on GL and on WebGPU. A gate picks a level and its controls over
the debug channel too (`dm set NAME VALUE`, `dm restart`: `src/opts.h`), never through the environment. Each
gate steps frames in long ranges, at most two hosts at once (`tests/lib/romdev.mjs`).

| Command | Checks |
|---|---|
| `node tests/destructo-physics.mjs` | the measurements above, in the cart, each with a control that must fail; the solver runs on the cart's workers (the pool beats one thread at 2000, 4000 and 6000 bodies) |
| `node tests/destructo-levels.mjs` | every level stands still for a minute, nothing floats or is placed inside another, the bot clears it within par; the level is judged only once still and its stars and headline match the structure 5 s later; the replay ends on the live state; each landmark's mark is reachable (controls: the old result rules, a nudged replay) |
| `node tests/destructo-landmarks.mjs` | every landmark: as built every block stands on something and none is inside another, after its settle every block lies on something and nothing moved 0.6 m, and the bot clears it within par (controls: a block put in the air, as built and after the settle; the bot aiming 150 milliradians off) |
| `node tests/destructo-human.mjs` | eight human-like players (src/human.c) on every level with the ammunition it gives: every one clears level 1, every level is cleared by 7 of 8, 97% of all plays clear; a steadier player is within par on two levels in three and more often than the newcomer; players go round to a hidden crystal (controls: the cannon before the fix, the ball breaking only the block it touches; the player who never goes round) |
| `node tests/destructo-soak.mjs [--quick]` | the bot level after level, 1 to 4 players: no NaN, no jitter left awake, no memory growth, every replay on the live state; and free fire on Taj Mahal (control: a NaN put into a block) |
| `node tests/destructo-result.mjs` | in the game: the result judged with at most 10 bodies awake, the replay ends on the live state (controls: the old rules, a nudged replay) |
| `node tests/destructo-flash.mjs` | every frame of Taj Mahal's blast through the engine's flash check (WCAG 2.3.1's general and red flash thresholds, small lights switching together, regular flicker) (controls: the game's 10 Hz wash and the engine's 8 Hz strobe) |
| `node tests/destructo-perf.mjs` | the frame's GPU time (romdev's `frame({op:'timing'})`) during Taj Mahal's collapse at 1920x1080 on High, GL and WebGPU, on the 890M, within 6 ms (controls: GL at 3840x2160, WebGPU against 0.5 ms) |
| `node tests/destructo-hitch.mjs` | the most CPU any frame of Taj Mahal's blast takes in the cart, timed by romdev, under 16.6 ms, GL and WebGPU (control: the game before the fix) |
| `node tests/destructo-look.mjs` | one picture of each setting: mean luma 95 to 125 and a deviation of 55 or more (never flat, never washed out) (control: three times the exposure) |
| `node tests/destructo-play.mjs` (`tests/gate.mjs --gates-file destructo-gates.json`) | a person's first shot: the same collapse on GL and WebGPU; two pads join and the second walks away, and the first still plays the second seat's turn (controls: a script that never fires; the seat rule before the fix waits on the idle pad) |
| `node tests/destructo-coplanar.mjs` | no two faces fight for the depth test on any level (the settled blocks held to their counts) (controls: the detector's selftest, a doubled face) |
