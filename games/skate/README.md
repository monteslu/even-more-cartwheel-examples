# skate (working title)

Skateboarding for one to four players in split screen. Five original parks (a concrete park with a bowl, a street
plaza, a mega ramp, rooftops, a timber yard of wooden ramps), a board that rides ramps as physics says, and flips,
grabs, spins, grinds and manuals linked into combos. Free skate, a two-minute score attack with each park's goals and
high score, and score battles.

| app | is |
|---|---|
| `skate` | the game |
| `skate_auto` | bots skate a score battle on each park in turn (hosts that send no input, the gates) |
| `skate_bench` | four bots in a four-way split on the mega ramp park (the perf gate's scene) |
| `skate_check` | native only: the ramp physics check, the park check, the soak |

Controls (pad, keyboard): steer and push on the stick or the d-pad (WASD, arrows); south pops (K, Space), west flips
(J), east grabs (L), north grinds (I), either shoulder is a manual (U, O), back goes to a start point (R). The stick
picks which flip or grab, and spins in the air.

`DESIGN.md` has the rules, the parks, how the board works, how the riders are posed and the tests. `tools/chars/make-skaters.mjs` makes the four
skaters; `tools/make-art.sh` makes the launcher art from the game's own frames.

```sh
node tests/skate-physics.mjs   # the board against ramp physics (a naive solver as the control)
node tests/skate-parks.mjs     # nothing floats, every line skateable, the bots' routes hold (four controls)
node tests/skate-soak.mjs      # 30 game minutes at 1 to 4 players
node tests/skate-feel.mjs      # a pad run and two bot runs: tricks land, the camera holds, the feet stand where a skater's do
node tests/gate.mjs --gates-file skate-gates.json
node tests/skate-perf.mjs      # the 890M at 1080p High, four-way split
node tests/skate-look.mjs      # each park's frames: contrast, not washed out; the sunset sky does not band
node tests/skate-cam.mjs       # the chase camera back behind the rider after vert air
node tests/skate-flash.mjs     # eight seconds of the bots through the engine's flash check (WCAG 2.3.1); an 8 Hz strobe must fail
node games/skate/tools/bake.mjs # rebake every park's lightmap (after a park changes shape)
```
