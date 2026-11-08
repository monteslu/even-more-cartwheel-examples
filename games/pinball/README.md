# pinball (working title)

Three original pinball tables for one to four players taking turns, built on cartwheel-engine and three.c.

| Table | Layout | Modes |
|---|---|---|
| Coral Deep | two ramps into the inlanes, two orbits with a spinner, three pops under three top lanes, a drop bank and a scoop | Tide Pool, Pearl Dive, Current, Leviathan |
| Star Foundry | a centre ramp that loops round the top, four pops in a diamond, an upper flipper, two drop banks, a saucer and a scoop | Cold Start, Sunpour, Shaping, Nova |
| Neon Fair | crossing ramps, a five-target shooting gallery, three pops, a drop pair, a scoop | Ticket Rush, High Striker, Gallery, Last Ride |

Every table has the same rule set: top lanes raise the bonus multiplier (the flippers move the lit lanes; the lane
blinking at the plunge is the skill shot), drop banks light the mode at the scoop and the kickback, ramps light the
lock, two locks start a three-ball multiball with a doubling jackpot, combos double and treble, all four modes light
an extra ball. Nudging works; too much warns twice and then tilts. The five best scores of each table are saved.

## Controls

Flippers: west and east, the shoulders, the d-pad's sides or the triggers (the keyboard's shifts, arrows, Z and /).
Hold the launch button (south, d-pad down, Space) to pull the plunger and let go to fire; once no ball waits on the
plunger, a fresh press of it flips both flippers. Nudge: north or d-pad up (the stick's sides nudge sideways). Start
pauses.

A flipper moves on the simulation step of the frame its button arrives in, reaches full stroke 24 ms after it (full
speed in 9 ms) and is back at rest 28 ms after the button is let go. The flippers stay live until the ball has gone
down the drain and through the bonus count, in ball save and multiball, a ball already past them included; only a
tilt kills them. The balls are drawn where the simulation has them on the latest step, as the flippers are.

## The ball

The ball runs on the game's own solver (src/phys.c), not the engine's Box2D or Box3D: `node tests/pinball-physics.mjs`
fires the same shots at all of them. With the flipper turning at the game's 52 rad/s and 60 ticks a
second, the engine's worlds let the ball through a firing flipper (Box2D 2 of 81 shots, Box3D 3 of 81) and from the
cradle (Box2D 3 of 7, Box3D 5 of 7), and shoot it at well under the speed rigid-body theory gives; stepped eight times
a tick Box2D holds and Box3D still lets it through (6 of 81). The game's solver substeps (half the ball's
radius, or 0.02 rad of flipper turn, per substep), finds every contact by conservative advancement along the
substep (flipper turning included) and passes them all.

## Apps and tests

| | |
|---|---|
| `pinball`, `pinball_auto`, `pinball_bench` | the game; the bot playing it; three balls on a table all the time (the perf gate) |
| `pinball_physcheck` | the solver comparison (`tests/pinball-physics.mjs`) |
| `pinball_flipcheck` | when the flipper buttons work (`tests/pinball-drain.mjs`): scripted balls past the flippers, into the drain, off the kickback, back up, in ball save and in a tilt; `pinball_flipcheck_oldrule` is its control |
| `pinball_soak` | the rules and the bot headless, game after game (`tests/pinball-soak.mjs`: traps, NaN, memory) |
| `pinball_tablemap` | a top-down picture of a table's physics and the bot's ball, for laying tables out |
| `tests/pinball-perf.mjs` | GPU time at 1080p High on the test GPU, GL and WebGPU, every table, three balls in play: 1.6 ms GL and 1.9 to 2.0 ms WebGPU on the Radeon 890M (bound 4.1) |
| `tests/pinball-gates.json` | `pinball-play`: a person's start (title, join, table, plunger, flippers) gives the same game bit for bit natively and in the dual cart on romdev; `pinball-face`: the face buttons alone flip (every press counted), the same on both |
| `tests/pinball-flippers.mjs` | every flipper binding (shoulders, triggers, d-pad, face buttons, keyboard) through the real input path, natively and on romdev: each press seen in its frame, the flipper moving on that step, full stroke in 15 to 25 ms, held without sag, back within two ticks; the control delays the buttons a tick and must fail |
