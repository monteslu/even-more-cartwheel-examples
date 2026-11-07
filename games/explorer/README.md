# explorer (working title)

A 2D exploration platformer on cartwheel: one great underground city of thirteen regions, where every ability you
find opens a way you could not take before. Six wardens, hidden relics, shrines to rest at and travel between, and a
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
| `explorer` | the game |
| `explorer_auto` | the bot plays it along the checker's route |
| `explorer_bench` | the perf gate's scene: a warden fight, both players |
| `explorer_worldgen` | builds `assets/world.bin` (native) |
| `explorer_check` | proves the world and writes `assets/route.bin` (native) |
| `explorer_feel` | measures the movement (native) |
| `explorer_soak` | the rules and the bot, no window, no GPU (native) |

## Tests

```sh
TARGETS=native scripts/build.sh explorer_worldgen explorer_check explorer_feel explorer_soak
node tests/explorer-world.mjs     # the world is current and proven; five broken worlds must each fail
node tests/explorer-feel.mjs      # jump heights, coyote time, jump buffer, with controls
node tests/explorer-soak.mjs      # the bot clears the game (every pickup, every warden), one player and two; a NaN must be caught
node tests/explorer-look.mjs      # frames of eight regions: value, contrast, a complementary accent; a flattened grade must fail
node tests/gate.mjs --gates-file explorer-gates.json   # GL and WebGPU, frames and state across hosts
node tests/explorer-perf.mjs      # 1080p High on the test GPU
```

Rebuilding the world: `build/native/project/games/explorer/explorer_worldgen games/explorer/assets/world.bin`, then
`build/native/project/games/explorer/explorer_check games/explorer/assets/world.bin --route games/explorer/assets/route.bin`
(in a build against the published SDK the tools sit in `build/native/games/explorer/`).
