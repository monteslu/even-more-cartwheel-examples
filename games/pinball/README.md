# pinball (working title)

Three original pinball tables for one to four players taking turns, built on cartwheel-engine and three.c.

| Table | Layout | Toy | Modes | Wizard mode |
|---|---|---|---|---|
| Coral Deep | two ramps into the inlanes, two orbits with a spinner, three pops under three top lanes, a drop bank and a scoop | a scaled creature that rises out of the playfield in front of the scoop | Tide Pool (a roving shot), Pearl Dive (a hurry-up on the ramps), Current (two balls, any lit shot), Leviathan (hit the creature) | The Abyss |
| Star Foundry | a centre ramp that loops round the top, four pops in a diamond, an upper flipper, two drop banks, a saucer and a scoop | a turning forge ring over a magnet that pulls the ball in, holds it and throws it | Cold Start (every target), Sunpour (a hurry-up on the ramp and saucer), Shaping (the magnet and the orbits), Nova (two balls, all five shots) | Supernova |
| Neon Fair | crossing ramps, a five-target shooting gallery, three pops, a drop pair, a scoop | a high striker behind the top arc: a ramp hit hard sends its puck up to ring the bell | Ticket Rush (a roving shot), High Striker (ring the bell), Gallery (every target), Last Ride (two balls, all five shots) | Grand Finale |

Shared by every table: top lanes raise the bonus multiplier (the flippers move the lit lanes; the lane lit at the
plunge is the skill shot), drop banks light the mode at the scoop and the kickback, ramps light the lock, two locks
start a three-ball multiball with a doubling jackpot, combos double and treble, all four modes won light an extra ball.
Each table's four modes play their own way: a roving shot that moves on every few seconds, a hurry-up whose value
counts down until a shot collects it, a sweep of every standup and drop target, the table's toy, two-ball modes. All
four played (won or not) light the table's wizard mode at the scoop: four balls, a long ball save, every shot and the
toy lit, each hit worth more than the last (and more for every mode won); when it ends the modes start over. Nudging
works; too much warns twice and then tilts. The five best scores of each table are saved.

Practice (below one player on the table select): the ball always comes back, and the left panel says how to aim.

## Starting

Play on the title goes straight to the table select, on the table played last; the launch button plays it. Two presses
from the title to the plunger. Players take turns, so the table select counts them; every pad plays (one passed round,
or each player on their own).

## Look and feel

The machine stands in a dim arcade between the other two tables, on a patterned carpet, with neon on the back wall.
Beside the table, the backbox: each table's own lit picture (painted in code: src/backbox.c) over the dot display. On
the right, the scores, the next thing to shoot for, and the table's modes and wizard mode as they stand.

Nothing on the table or the screens strobes. Every light (src/lights.c) swells and fades and turns from brightening to
dimming at most two or three times a second however fast the rules toggle it; a lamp the rules pulse is a smooth wave
of at most 1.1 cycles a second; the flashers rise over a few frames, hold and fade, and fire again only after they
have faded; the general illumination dips for a moment at a mode start and comes back over half a second; the dot
display slides and brightens its messages instead of blinking them. `tests/pinball-flash.mjs` measures it.

The pad rumbles with the machine: a tap for each flip and coil, a thump for a drain or a nudge, a long roll for a
jackpot, multiball and the wizard mode. The camera never moves for a nudge or a hit.

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
| `pinball_soak` | the rules and the bot headless, game after game (`tests/pinball-soak.mjs`: traps, NaN, memory; each table's toy kept busy: no trap, no ball held, no ball through the creature) |
| `pinball_rulecheck` | each table's modes, toy and wizard mode through the rules' own switch events (`tests/pinball-rules.mjs`); `pinball_rulecheck_oneset` (one shared rule set, no toys) is its control |
| `pinball_flashcheck` | every light's level over play, modes, wizard modes and event storms (`tests/pinball-flash.mjs`, with the frames of `pinball_bench` in a storm); the `_oldstrobe` builds are its controls |
| `pinball_tablemap` | a top-down picture of a table's physics and the bot's ball, for laying tables out |
| `tests/pinball-perf.mjs` | GPU time at 1080p High on the test GPU, GL and WebGPU, every table, three balls in play: 1.6 ms GL and 1.9 to 2.0 ms WebGPU on the Radeon 890M (bound 4.1) |
| `tests/pinball-gates.json` | `pinball-play`: a person's start (title, table, plunger, flippers: two presses to the plunger) gives the same game bit for bit natively and in the dual cart on romdev; `pinball-face`: the face buttons alone flip (every press counted), the same on both |
| `tests/pinball-flippers.mjs` | every flipper binding (shoulders, triggers, d-pad, face buttons, keyboard) through the real input path, natively and on romdev: each press seen in its frame, the flipper moving on that step, full stroke in 15 to 25 ms, held without sag, back within two ticks; the control delays the buttons a tick and must fail |
