# even-more-cartwheel-examples

[![CI](https://github.com/monteslu/even-more-cartwheel-examples/actions/workflows/ci.yml/badge.svg)](https://github.com/monteslu/even-more-cartwheel-examples/actions/workflows/ci.yml)

Even more games built with
[cartwheel-engine](https://github.com/monteslu/cartwheel-engine), a 2D and 3D
game engine in C, after the ones in
[cartwheel-examples](https://github.com/monteslu/cartwheel-examples) and
[more-cartwheel-examples](https://github.com/monteslu/more-cartwheel-examples).
Each game is written to push the engine, and the generic fixes it finds go back
into the engine.

The games ship as [wasmcart](https://github.com/wasmcart/wasmcart) carts
(`.wasc`): one file per game that runs in a browser, in Node, in a native
player and in RetroArch, with GL and WebGPU in the same cart.

## The games

### towers

![towers: two players hold a desert map against a swarm](docs/screenshots/towers.jpg)

A tower defense game for one to four players on one screen (working title).
Each player's cursor hops between the build pads in their own share of the
map, and the face buttons build four towers laid out as a diamond round it:
chain lightning, frost, a ramping beam and a mortar, each upgradable to level
3. Waves of eight creep kinds grow into the thousands, with a boss every tenth
wave. 30 maps in six themes, each checked in CI: the path, the pads, fair
shares, grounding, the baked light and a clear by two bots.

### pinball

![pinball: the Coral Deep table with its two ramps](docs/screenshots/pinball.jpg)

Pinball for one to four players taking turns (working title), on three
original tables: Coral Deep, Star Foundry and Neon Fair. Ramps, pop bumpers,
drop targets, four modes per table, a three-ball multiball, nudge and tilt, and
a saved top five per table. The ball runs on the game's own solver, swept and
substepped so it never passes through a wall or a flipper at any speed.

### skate

![skate: four players in a four-way split at the Timber Yard's wooden ramps](docs/screenshots/skate.jpg)

Skateboarding for one to four players in split screen (working title), in five
original parks, each with its own setting to the horizon and its own light: a
concrete park with a deep bowl by a beach boardwalk at golden hour, a street plaza
with stairs and handrails in a city block, a mega ramp out under a big sky,
rooftops over a downtown street at sunset and a yard of wooden ramps in a lumber
yard among pine hills. Each park is lightmapped.
Flips, grabs, spins, grinds and manuals link into combos (a bail loses them);
free skate, a two-minute score attack with each park's goals and high score,
and score battles. The board runs on the game's own controller, substepped and
swept against the park's triangles, so a quarter pipe gives back exactly the
height its speed earns; every park is checked in CI: nothing floats and a bot
skates every line.

More games are on the way.

## Building

```sh
scripts/fetch-deps.sh     # the pinned engine SDKs (scripts/deps.lock)
scripts/build.sh          # build/native and build/cart-wgpu (the carts)
```

`AGENTS.md` has the test commands and the rules for showing controls.

## License

MIT, see `LICENSE`. Each game's `CREDITS` file lists what it builds on.
