# skate (working title)

A skateboarding game for one to four players in split screen: open parks to skate, a board that rides ramps the way
physics says it should, flips, grabs, grinds and manuals linked into combos, and timed runs with goals and a high
score. Everything is original: the parks, the skaters, the trick names.

## Play

| | pad | keyboard |
|---|---|---|
| steer, push (up), brake (down), balance a manual (up and down) or a grind (left and right) | left stick or d-pad | WASD or the arrows |
| crouch, let go to pop (an ollie) | south | K or Space |
| flip (the stick picks which: nine flips) | west | J |
| grab, held for points (the stick picks which: nine grabs) | east | L |
| grind, near a rail, a ledge's lip or a coping | north | I |
| manual (or hold it into a landing to land in one) | either shoulder | U or O |
| back to a start point (each press the next one) | back | R |

A flip pressed on the ground pops and flips at once. In the air the stick spins the rider (spins score by the 180).
Landings are judged: the board's heading against the run along the surface (within 26 degrees clean, 46 sketchy and
slower, more a bail), its up against the surface, a flip still turning or a grab still held (a bail), how hard it came
down. A trick adds its points to the combo; the combo's sum is multiplied by its count of tricks and banked a fifth of
a second after a landing that a manual or a grind does not link. A bail loses the combo. The same trick twice in one
combo is worth half, then a quarter.

Modes: free skate (no clock), score attack (two minutes, the park's seven goals: three scores, its five tokens, a
10,000 point combo, its named gap, a grind of its named rail held long enough; the park's high score saved), score
battle (two minutes, two to four players, the most points wins). A pad joins mid run with its south button.

## The parks

Built in code from a kit (src/park.c): ground with holes, quarter pipes (a circular transition, vert above the radius,
a deck and steel coping), banks, kickers, bowls (a rounded rectangle swept with a quarter pipe's profile, coping round
the rim), boxes, ledges, rails on posts, stairs with handrails, low walls. Every piece writes its triangles (each with
smooth corner normals: a curve is a curve to the board), the edges to grind (linked end to end, so a grind carries on
round a bowl), and the points it stands on. src/parks.c lays out five:

- **Sunnyside**: a concrete park, a deep bowl with a little vert, two long quarter pipes facing across, a funbox,
  granite ledges, a flat bar, a kicker over a picnic table.
- **Civic Plaza**: a street plaza, a raised terrace with six-stair sets and handrails, banks up to it, ledges, a
  fountain with a grindable rim, manual pads.
- **Big Sky**: the mega ramp, a 14 m roll in, a kicker over a 10 m gap onto a landing slope, a giant quarter pipe,
  and a practice area.
- **Rooftops**: four roofs stepping down over the street, gaps to ollie, a kicker over the widest, parapets and rails
  to grind, AC units, a bank up from the street.
- **Timber Yard**: wooden ramps, a vert halfpipe, a spine, a mini ramp, a pyramid with banks on four sides.

Each park lists its lines: a start point, a speed to push to, and a feature (a quarter pipe's air, a rail, a ledge, a
gap, a manual pad). The back button cycles the start points.

**The bots** (src/bot.c, the auto cart's skaters) go from line to line on the board. A route planner (src/nav.c) lays
a half-metre grid over the park: each cell's floor, and everything that stands in the way (walls, ramps' faces,
rails, a ledge's side) as bands of height, so a cell is blocked for a rider when something is in it from 4 cm to
1.7 m over its floor. A bot moves to a neighbouring cell along the floor, up a bank only straight up its fall line
with room either side, or off an edge onto a floor up to 4.2 m below (never up one). After a line it floods the park
from where it is, picks a line whose start it can reach (the nearer the likelier, not the same one twice running)
and skates there through the real controls: it follows the route a few metres ahead, pushes to a cruising speed,
slows for a turn (a stop and a pivot for a turn about), takes a run at a bank and comes into the start along the
line's heading. A start no wheels reach (the mega ramp's roll in, a higher roof, a ramp's deck) is walked to: back,
as a person would carry the board up, about one start in five.

**The park check** (tests/skate-parks.mjs, `skate_check parks`): nothing floats (each piece's footing stands on
another piece within 2 cm, or on the ground), every line is skateable (a bot skates it through the real controls
and does its feature) and the bots' routes hold (two bots skate each park for three minutes: no route given up).
Its controls must fail: a piece lifted 30 cm, a wall across a rail's approach, a kicker cut short under its gap, a
planner that goes straight through the park (its routes run into things).

## The board

src/skater.c is a custom controller with swept contact against the park's triangles, not the engine's rigid bodies.
The decision was measured (tests/skate-physics.mjs): a body stepped once a tick on the mesh (the "naive" solver: one
discrete step, faceted normals, a contact test at the end of the step) sends 13 of 108 fast landings through a landing
slope; the game's solver sends none. At mega ramp speeds a tick is a third of a metre.

- Rolling: substeps of at most 5 cm. Gravity along the surface, the push, rolling resistance and air drag, the steer
  turning the velocity about the surface normal. The board is put back on the surface by a short cast along the normal
  and the velocity is turned with the normal (a rotation, so speed is kept: a quarter pipe's apex is v^2 / 2g within
  0.3% and it gives the speed back within 0.3%). A crest of more than 11 degrees at speed, or no surface under the
  board, and it is in the air. A wall ahead (a cast at the trucks' height) turns the velocity off it, or bails a rider
  who hits it head on and hard.
- The air: ballistic, the step from the last point to the next cast against every surface (no tunnelling at any
  speed). Off a vertical wall the flight keeps to the plane of the coping, 5 cm out over the ramp, so the rider comes
  back down onto the vert and keeps the speed (a 10 m/s vert air gives back 9.99 m/s). The board follows the flight's
  pitch, so a landing slope meets it square. Vert air is drawn with its real attitude: the board leaves at the angle of
  the ramp's face where it took off and turns toward level about the coping's line only as far as the air's height
  allows (a 2.5 m air a little over half way, at its top), back at the face's angle by the coping, so it lands
  square on the wall.
- Grinds: a press near a rail, a ledge's lip or a coping (within 55 cm across, moving along it) snaps the board on;
  it slides with gravity and friction along the linked runs; the balance meter wobbles harder the longer it goes.
- Manuals: a balance meter on up and down.

## The look

Each park has a setting out to the horizon and a light of its own (src/scenery.c):

| park | setting | light |
|---|---|---|
| Sunnyside | a beach town: a boardwalk with palms along the east fence, sand with huts, umbrellas and lifeguard towers, a pier into the sea; pastel two-storey streets to the west, wooded hills behind | golden hour: the sun low, long shadows, warm haze |
| Civic Plaza | a city block: a ring road with lanes, crossings and parked cars, sidewalks with lamps and street trees, mid-rise facades with shopfronts and awnings, towers further out, a skyline | a bright hazy morning |
| Big Sky | an event in the open: golden grassland, a gravel car park of cars and vans, tents and bleachers by the mega ramp, banners; mesas and mountains far off | clear noon under a deep blue sky |
| Rooftops | downtown at sunset: the street runs on past both fences, taller buildings close in with lit windows and neon blade signs, a skyline all round | sunset, the windows and the signs coming on |
| Timber Yard | a lumber yard: board stacks on bearers, log piles, a sawmill shed of corrugated sheet; pine forest on the hills round it, mountains behind | an early morning: gold light through cool air |

How it is drawn: the near scenery is merged into one mesh per surface (its colour map only: it is seen from tens of
metres off, and nothing out there casts into or takes from the sun's map) (facades, brick, glass, roofs, trim, paint, steel,
wood, sheet metal, lumber, car paint, the lamps' glow), each a procedural texture with world-scaled coordinates, so a
window is a window's size on every building: a whole town is a dozen draws. Trees are layered cards of a foliage texture (alpha cut, their
normals out from the crown so it shades as a volume), instanced; past 220 m a tree is two or three crossed cards.
Cars are extruded side profiles (bonnet, raked windscreens, glasshouse, wheels with hubs). Clouds are cumulus cards
with sunlit tops. The near facades carry sill courses and piers that stand out of the wall. A colour grade (a .cube
LUT from tools/make-lut.mjs: a gentle S-curve and a little saturation) follows the tone curve. The ground is one radial mesh round the park, flat near it and rising into hills and
mountains far off; the far skyline is an impostor, a band of towers painted on a ring at 650 m (alpha cut). Height fog
in each park's haze colour, with the sun's scatter, gives the distance its air; the sky dome is the mood's own gradient
and clouds, dithered (a noise texture finer than a pixel, a level or so of the screen's 8 bits deep): a sunset's
gradient over a few levels would otherwise band.

The park itself is lightmapped: its surfaces are baked (`SKATE_BAKE=<dir>` on a native build writes
`assets/lightmaps/park<n>.cwlm`): the sky's light with its occlusion (the dark under a coping's lip, the corner where a
ramp meets the ground, the inside of a bowl) and the sun's bounce. The sun's direct light stays real time, so the
skaters cast their shadows on the park and the ramps on each other. The surfaces are procedural PBR sets (src/gfx.c):
trowelled concrete with joints, hairline cracks, water stains and worn wheel paths; asphalt with sealed cracks and
patches; plywood with seams, screw rows and scuffs; painted steel with chipped flakes and scratches; pavers; brick
with windows. Decals on top: wax along every ledge and coping (dark and glossy), our own tags and stickers on the
walls, skid marks on the ground, each kept only where all four corners lie on its surface.

The skaters are skinned glbs made by tools/chars/make-skaters.mjs (the brawler example's character generator, copied
and given four skaters' outfits). src/pose.c poses them every frame from the board's state, no clips: crouch, tuck,
lean, push phase and balance eased toward what the board does (a tenth of a second), the legs solved with two-bone IK
so the feet stay planted on the deck through a crouch, a flip catching the board or a manual tipping it, a hand on the
board for a grab. A flip turns the board under the rider about its own axes; the wheels turn with the speed. A bail is
a ragdoll (src/rag.c, after the brawler's) tumbling over the park's triangles, presentation only; the camera follows
the body from further back. A landing kicks up a puff of dust; a grind throws sparks.

The cameras ride low behind each skater along its run, pull back and widen the lens with speed (62 to 76 degrees),
and in vert air stay down at the coping's height and look up at the rider, so the height shows. When the run turns
right about (down out of vert air, rolling back from a wall) the camera swings round the rider at up to 2.2 radians a
second, to the side it last swung to. They pull in when a wall is in the way. No camera ever shakes.

## Tests

| test | checks |
|---|---|
| tests/skate-physics.mjs | the board against ramp physics; the naive solver as the control |
| tests/skate-parks.mjs | nothing floats, every line skateable, the bots' routes between lines hold; four controls (a straight-line planner among them) and each park's bake made for its shape (a lifted piece as the control) |
| tests/skate-soak.mjs | 30 game minutes at 1 to 4 players, every park; a NaN control |
| tests/skate-feel.mjs | a person's twelve seconds on a pad: every trick lands, the camera never jerks, the feet stay on the deck; a mistimed flip as the control |
| tests/skate-gates.json | the auto cart on every host, frames and simulation state exact |
| tests/skate-perf.mjs | the 890M at 1080p High, four-way split |
| tests/skate-look.mjs | every park's chase-camera frames have contrast and are not washed out: mean luma 95 to 128, its spread 55 or more; two stops over as the control. The sunset sky does not band (40% or fewer of its pixels the colour of the one above; undithered as the control) |
| tests/skate-cam.mjs | the chase camera comes back behind a rider after vert air (behind on 80% of the frames from one to two and a half seconds after each landing, never within 2.3 m); the old lerp toward the run as the control |

## Performance

The perf gate's scene (skate_bench): four bots on the mega ramp park in a four-way split, 1920x1080, High, on the
890M: 2.6 ms of GPU a frame on GL, 3.8 ms on WebGPU (the bound is 4.5; a two-player split on each other park, 1.9 to 2.8 ms on GL and 3.2 to 4.1 ms on WebGPU), settings to the horizon included. What paid for it: the scenery merged into a mesh per surface and the forests instanced (a whole town is a dozen draws), the park's indirect light baked (no screen-space occlusion), no bloom (a sunny
afternoon has little to bloom; 0.8 ms), the sun's map at 2048 texels when the screen is split three or four ways (a
view is a quarter of the screen), one shadow map for every view (the pipeline draws it with the first view and the
others reuse it), one mesh per surface kind for the whole park.
