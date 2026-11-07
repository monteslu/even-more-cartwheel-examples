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

**The park check** (tests/skate-parks.mjs, `skate_check parks`): nothing floats (each piece's footing stands on
another piece within 2 cm, or on the ground) and every line is skateable (a bot skates it through the real controls
and does its feature). Its controls must fail: a piece lifted 30 cm, a wall across a rail's approach, a kicker cut
short under its gap.

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
  pitch, so a landing slope meets it square.
- Grinds: a press near a rail, a ledge's lip or a coping (within 55 cm across, moving along it) snaps the board on;
  it slides with gravity and friction along the linked runs; the balance meter wobbles harder the longer it goes.
- Manuals: a balance meter on up and down.

## The look

Sunny, colourful, clear: a ProceduralSky in the early afternoon drives the sun; one shadow map over the whole park
(the park holds still, so it is drawn once a frame, not once a view); a light haze; AgX tone mapping. The park's
surfaces are procedural PBR sets (src/gfx.c: trowelled concrete with its joints, plywood with seams and screws, brushed
steel, pavers, brick with windows), tinted by their vertex colours: teal, orange, yellow, magenta and blue ramps, warm
concrete, green grass.

The skaters are skinned glbs made by tools/chars/make-skaters.mjs (the brawler example's character generator, copied
and given four skaters' outfits). src/pose.c poses them every frame from the board's state, no clips: crouch, tuck,
lean, push phase and balance eased toward what the board does (a tenth of a second), the legs solved with two-bone IK
so the feet stay planted on the deck through a crouch, a flip catching the board or a manual tipping it, a hand on the
board for a grab. A flip turns the board under the rider about its own axes. A bail is a ragdoll (src/rag.c, after the
brawler's) tumbling over the park's triangles, presentation only.

The cameras follow behind each skater along its run, hold still through vert air (the rider comes back down into the
picture) and pull in when a wall is in the way. No camera ever shakes.

## Tests

| test | checks |
|---|---|
| tests/skate-physics.mjs | the board against ramp physics; the naive solver as the control |
| tests/skate-parks.mjs | nothing floats, every line skateable; three controls |
| tests/skate-soak.mjs | 30 game minutes at 1 to 4 players, every park; a NaN control |
| tests/skate-gates.json | the auto cart on every host, frames and simulation state exact |
| tests/skate-perf.mjs | the 890M at 1080p High, four-way split |
