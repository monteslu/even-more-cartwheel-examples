# Ember Throne: design

A third-person action adventure in one connected world: explore, fight in close quarters with a sword, find what
opens new ways through, beat three bosses. One player; a second can drop in beside the first at any time. The wanderer gathers the Sun Key and the Moon Key to open the Ember Keep and face the Ember King on his throne.

## The loop

Rest at a shrine (health, the flask refilled, the foes back, the save), go out, fight the groups on the way, find
something (a heart vessel, the stamina charm, the maul, a key), open a way you could not open before, and find the
shortcut back to a shrine you know. A fall wakes you at the last shrine; bosses stay beaten.

The order through the world:

1. The Green Hollow (start, shrine). North up the stairs.
2. The Amber Canyon (shrine). The terrace chest: the Stonebreaker (a maul: it breaks cracked stone). The west
   ledge: a heart. Up the north stairs: the Thornwarden, who gives the Sun Key and a heart.
3. The maul breaks the cracked wall across the passage east: the Tidemarsh (shrine). An island: the stamina charm.
   The temple: the Tidecaller, who gives the Moon Key and a heart. A lever opens the door west, back into the hollow.
4. Back in the hollow the maul opens the ruined cellar (a heart). The keep's gate takes both keys.
5. East of the marsh, the Glowcap Wood (shrine): a second stamina charm in a chest among the giant mushrooms.
6. North of the marsh a switchback road climbs the cliffs to the Frostspine Heights (shrine): a heart in a chest. The
   cliff path runs west over a timber bridge (under it the gorge floor, a way of its own with a heart at its end) and
   down into the Thornwarden's ring: the long way round comes back as a shortcut.
7. South of the hollow, through the cellar, the Underway: a roofed cutting lit by lanterns, out onto the Windward
   Downs (shrine), where a stair winds round the Spire to a heart on its top.
8. The Ember Keep: the courtyard (shrine), the long walled ramp (its side door opens back down to the courtyard),
   the throne top: the Ember King. The end.

## Combat

- Light cuts chain three deep inside a window near the end of each; a heavy spins and knocks ordinary foes down.
  The maul is slower and hits harder; its heavy is a ground slam.
- The dodge is a dive roll with invulnerable frames through most of it (2 to 21 of 37 ticks); with no direction, a
  quick back step (1 to 13 of 25). Rolling through a shockwave is the answer to the bosses' later phases.
- Stamina pays for every swing and roll and comes back after a short wait, so a fight has a rhythm.
- Lock-on: the player faces the target, moves in a circling walk with sidestep and back-step clips, and the camera
  turns to keep both in view; a flick of the camera stick moves to the next foe that side.
- Foes: raiders (quick cleavers, a slower overhead), brutes (hyper-armoured through their swings, a slam with a ring on
  the ground, a looping haymaker), lancers (long reach, they keep their distance). A group wakes together; two at a
  time come at each player (an attack token), the rest circle. Every blow has a wind-up of 26 frames or more, with a
  warm flare of light on the foe and a sound; area blows draw a ring that grows to the blow's reach.
- Bosses: phases at two thirds and one third of their health, each with a roar (invulnerable) and quicker, newer
  blows: charges across the arena, and shockwaves that run out along the ground.

## The camera

An orbit behind the player, part of the simulation (it decides what "up the stick" means, so the gates compare it).
It follows lazily behind the way the player runs when the camera stick is left alone, frames a lock-on target, pulls
in instantly when a sphere swept from the player's head out to the camera meets a wall (and eases back out), and
never sinks under the ground. With two players it frames both.

## The world

One 640 m terrain (Terrain3D, 1 m samples) shaped from tables: zones (the meadow bowl, the canyon floor, the boss
rings, the marsh basin, the keep hill, the heights plateau and its gorge, the underway cutting, the downs round the
spire, the wood), ramps between them (stairs, switchbacks, a cliff path), highlands between the regions and mountains
round the edge. Painted by zone and slope, with soft edges where a path meets the grass; grass and stones scattered by
the engine. Trees are the game's own: nine species (oak, birch, aspen, willow, pine, fir, windswept, deepwood and the
glowcap), a few per region, each a trunk and a crown of alpha-cut leaf cards, drawn in 48 m tiles (one instanced mesh
per species, region and tile): full crowns that sway in the wind within 70 m, an impostor crown of a few large clusters
out to 190 m, nothing past that (the haze has them). The paths are packed earth with angular gravel; the cliffs are
strata of violet, grey and rust beds split by joints and cracks; the grass is short and dense, fading out by 20 m. A cloud layer drifts over the sky. Buildings are boxes
from tables (walls, ruined houses, stairs as solid steps, towers), merged into one mesh per material and region and
one static body per region. Props are glTF instances seated on the lowest ground under their footprint. Paging: the
regions more than one step from the player's are hidden and their foes stop. Walls carry broken tops and rubble at
their feet; barrels, crates, sacks, carts and lantern posts dress the villages.

Mood comes from colour, not darkness: each region has its own light (fog colour and density, ambient, sky and ground
light, the sun's colour, exposure), blended by where the camera is: a clear gold morning in the hollow, amber in the
canyon under a cool lavender haze, teal and violet mist on the marsh, a crimson sunset at the keep, cold bright air on
the heights, lantern light in the underway, deep green and a violet glow in the wood. A contrast grade (an S-curve
with a black point, a 3D LUT after tone mapping) keeps every region mid-key with real darks (tests/emberthrone-look.mjs).

## Characters

Generated by the brawler's character generator (copied into tools/chars) with this game's cast: the wayfarer (player
one), the warden (player two), raiders, brutes, lancers, and the three bosses at 1.5 to 1.95 times a person's size.
Clips for this game: the sword idle and run, the three cuts, the heavy spin, the dive roll and back step, the
sidesteps and back walk, the kneel and rest at a shrine, the drink, the push, the roar. Feet are planted on slopes and
stairs (FootPlacement3D on the physics ground); the dead fall as ragdolls.

## Tests

- `tests/emberthrone-gates.json`: the bot's first 30 seconds (it rests, crosses the hollow, fights the raiders on the road
  and reaches the canyon shrine) and a scripted pad run (Start, Start, the road, the lock, three cuts, a roll, a heavy, a
  drink) with its control. Both agree to the bit across native GL, native WebGPU and the cart on romdev's WebGPU host.
- `tests/emberthrone-soak.mjs`: the whole game played by the bot on software GL, 30 rule ticks a frame: it wins (18.5
  minutes of game time through all eight regions: 50 foes, all three bosses, both keys, the maul, every shrine, no
  falls), positions stay finite, the camera never has a wall between it and the player, the route never stalls 6
  minutes, memory holds (1 percent). Control: an empty maul chest (the cracked wall never breaks) must fail. The two
  halves run as two suites (`--no-control`, `--control-only`) to keep each hold of the GPU suite lock short.
- `tests/emberthrone-grounding.mjs`: 106 props, 546 building boxes and 1383 scattered stones on the ground (worst 0 cm).
  Control: one tree lifted half a metre must be caught.
- `tests/emberthrone-look.mjs` (frames from tools/capture-look.sh): each region's play frame is mid-key (mean luma 100
  to 125), has real contrast (luma deviation 55 or more, local contrast 13 or more), a complementary accent and a
  saturation in range. Control: the frames flattened halfway to their mean must fail.
- `tests/emberthrone-text.json` (the engine's tools/check-text.mjs): the HUD at the readable floor on three screens.
- `tests/emberthrone-perf.mjs`: 1080p High on the 890M, GL and WebGPU, eight places (the marsh boss fight, the meadow,
  the Thornwarden, the glowcap wood, the heights, the underway, the spire, the Ember King) against 4.5 ms. Controls: 4K
  GL and an impossible 1 ms bound must fail. `tests/emberthrone-perf-probe.mjs` takes parts out (ADV_PROBE_*) and compares
  medians of interleaved runs. What moved the frame: the terrain no longer casts the sun's shadow (about a quarter of the
  WebGPU frame), a depth pass first (the heights from 5.7 to 3.7 ms on WebGPU), the marsh water drawn only near the
  marsh, lighter crowns and sparser groves, impostor crowns for far trees, bloom over four levels, the terrain's full
  shading to 110 m. The meadow, the spire and the wood stay over the bound (about 4.7 to 5.9 ms): with every part the
  probe can take out gone (terrain, trees, grass, stones, buildings, characters, water, clouds, sky background) the
  wood's scene pass still costs 3.7 ms on GL against 2.6 ms at the heights, so the remaining cost is not in the
  game's content; see the probe's `bench_terrain` and `bench` lines.
