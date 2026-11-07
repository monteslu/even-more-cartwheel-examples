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

In progress:

- **towers**: a tower defense game for one to four players: build along a
  winding route and hold off waves of thousands.
- **pinball**: pinball tables with real ball physics, lit and rendered in 3D.
- **demolition**: bring down towers, walls, bridges, castles and scaffolds of wood,
  stone, glass and steel with a launcher, for one to four players taking turns.

## Building

```sh
scripts/fetch-deps.sh     # the pinned engine SDKs (scripts/deps.lock)
scripts/build.sh          # build/native and build/cart-wgpu (the carts)
```

`AGENTS.md` has the test commands and the rules for showing controls.

## License

MIT, see `LICENSE`. Each game's `CREDITS` file lists what it builds on.
