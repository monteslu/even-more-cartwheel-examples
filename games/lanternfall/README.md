# Lanternfall

A 2D exploration platformer on cartwheel: the lanterns of the old city have gone out, and the hooded lamplighter
wakes its beacons again across one great underground city of thirteen regions, where every ability you
find opens a way you could not take before. Six wardens with two phases each, nine kinds of creature, trials that lock an upgrade's room, relics that each
give a charm, shrines to rest at and travel between, and a
map that fills in as you go. A second player can drop in on a pad. `DESIGN.md` has the design.

## Controls

| | pad | keyboard |
|---|---|---|
| move, aim | d-pad or left stick | arrows or WASD |
| jump | south | K, Space |
| strike | west | J |
| dash (down + dash in the air: plunge) | east | L, Left Shift |
| spark (down + spark on the ground: mend) | north | I |
| grapple line | shoulders | U |
| map | Back | M, Tab |
| rest at a shrine | up beside it | up |

## Apps

| app | |
|---|---|
| `lanternfall` | the game |
| `lanternfall_auto` | the bot plays it along the checker's route |
| `lanternfall_bench` | the perf gate's scene: a warden fight, both players |
| `lanternfall_worldgen` | builds `assets/world.bin` (native) |
| `lanternfall_check` | proves the world and writes `assets/route.bin` (native) |
| `lanternfall_feel` | measures the movement (native) |
| `lanternfall_rules` | measures the combat rules (native) |
| `lanternfall_soak` | the rules and the bot, no window, no GPU (native) |

## Tests

```sh
TARGETS=native scripts/build.sh lanternfall_worldgen lanternfall_check lanternfall_feel lanternfall_rules lanternfall_soak
node tests/lanternfall-world.mjs     # the world is current and proven; five broken worlds must each fail
node tests/lanternfall-feel.mjs      # jump heights, coyote time, jump buffer, the dash off a wall, the sprint, with controls
node tests/lanternfall-rules.mjs     # the hold after a blow, knockback, trials, warden phases, charms, with controls
node tests/lanternfall-soak.mjs      # the bot clears the game (every pickup, every warden), one player and two, two runs each; a NaN must be caught
node tests/lanternfall-look.mjs      # frames of eight regions: value, contrast, a complementary accent; a flattened grade must fail
node tests/gate.mjs --gates-file lanternfall-gates.json   # GL and WebGPU, frames and state across hosts
node tests/lanternfall-perf.mjs      # 1080p High on the test GPU
```

Rebuilding the world: `build/native/project/games/lanternfall/lanternfall_worldgen games/lanternfall/assets/world.bin`, then
`build/native/project/games/lanternfall/lanternfall_check games/lanternfall/assets/world.bin --route games/lanternfall/assets/route.bin`
(in a build against the published SDK the tools sit in `build/native/games/lanternfall/`).
