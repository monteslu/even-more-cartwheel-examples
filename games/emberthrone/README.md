# Ember Throne

A third-person action adventure through one connected world, for one player, with a second able to drop in at any
time. The wanderer gathers the Sun Key and the Moon Key to open the Ember Keep and face the Ember King on his throne. `games/emberthrone/`.

## The world

Eight regions on one 640 m terrain, joined so that the way back is often a shortcut:

- **The Green Hollow**, a meadow with a ruined village and the first shrine. Stairs climb north to the canyon; the
  keep's gate stands to the west (it wants two keys); a door to the east only opens from the marsh side.
- **The Amber Canyon**, sandstone under violet cliffs. A terrace holds the Stonebreaker, a maul that breaks cracked
  stone; a ledge holds a heart vessel; a ring of pillars at the top of the north stairs holds the Thornwarden. A
  cracked wall closes the passage east.
- **The Tidemarsh**, teal pools and stilt trees, slow to wade. A stamina charm waits on an island; the Tidecaller holds
  the marble temple at the south end; a lever opens the door back to the hollow.
- **The Ember Keep**, a red keep on an ember hill: a courtyard with a shrine, a long walled ramp, and the Ember King
  on the throne top.
- **The Frostspine Heights**, a snowy plateau up a switchback road cut into the cliffs east of the marsh, with a
  heart in a chest. A cliff path runs west from it over a timber bridge (the gorge floor under it is a way of its
  own, with a heart at its end) and down into the Thornwarden's ring: the long way round becomes a shortcut.
- **The Underway**, a roofed cutting under the hollow's south hills, lit by lanterns.
- **The Windward Spire**, open downs with a stone tower: a stair winds round it to a chest on the top.
- **The Glowcap Wood**, a deep wood of giant glowing mushrooms east of the marsh, with a second stamina charm.

Things to find: seven heart vessels (five in the world, one from each of the first two bosses), two stamina charms, the
Stonebreaker, the Sun Key and the Moon Key. Shrines heal you, refill the flask, bring the foes back and save; a fall
wakes you at the last one you rested at.

## Controls

The screens show each control with the engine's button prompts for the device in hand.

| Action | Does |
|---|---|
| move | the left stick or the d-pad; the camera follows by itself (the right stick turns it) |
| attack | a light cut; press again for the second and third of the combo (the maul: an overhead) |
| heavy | a spinning cut that knocks foes down (the maul: a ground slam) |
| dodge | a roll the way you move, invulnerable through most of it; with no direction, a back step |
| lock | lock on to the foe in view: you face it, the camera keeps you both in view; flick the right stick for the next one |
| use | rest at a shrine, open a chest, pull a lever, unlock the gate; with nothing near, change weapon |
| tonic | drink from the flask |
| swap | change weapon |

Every action costs stamina; it comes back when you stop spending it. A blow that lands stops the world for a moment
and is felt in the pad; break a foe's poise and your next blow is a critical. Foes come at you two at a time while the
rest circle round behind you; every blow has a wind-up (a flare of light on the foe), and area blows a ring on the
ground. Shield bearers turn cuts from the front (go round them, or break the shield with a heavy), slingers throw at
whoever stands still, and something waits under the marsh pools. Beat a whole group and the flask gets a sip back.
Bosses change in their later phases: charges, strings of blows, and shockwaves that run out along the ground (roll
through them); after their biggest blows they stand spent for a moment, open to you.

## Builds and tools

| | |
|---|---|
| `emberthrone` | the game |
| `emberthrone_auto` | the bot plays the whole game through the controls (hosts that send no input, the gates, the soak) |
| `emberthrone_bench` | the perf gate's scene: the bot in the Tidecaller's fight |
| `tools/chars/build-cast.sh` | the characters: CC0 bases dressed and recoloured in headless Blender (`tools/chars/blender`), then put on the game's skeleton with its clips (`tools/chars/make-characters.mjs`) |
| `tools/capture-chars.sh` | close-ups of the cast at the game camera's distance and at a portrait's |
| `tools/make-world-assets.mjs` | the terrain layers, building textures, props, the trees and glowcaps (leaf cards), the cloud layer and the contrast grade |
| `tools/survey.sh` | the program table from software GL runs (off the shared GPU) |
| `tools/swshot.sh` | a screenshot on software GL |
| `tools/capture-look.sh` | five play frames from each region spread over its fight, for the look check (`LOOK_REGIONS` takes some regions) |

Tests: `node tests/emberthrone-rules.mjs` (the combat feel on the real rules, each rule with a control that must fail),
`node tests/emberthrone-flash.mjs` (the journey and a boss fight every frame against the photosensitive flash thresholds, with
strobing controls that must fail), `node tests/emberthrone-soak.mjs` (the whole game on software GL, with a control that must fail),
`node tests/emberthrone-grounding.mjs` (no floating props, with a control that must fail),
`node tests/emberthrone-perf.mjs` (1080p High on the 890M, GL and WebGPU), `node tests/emberthrone-look.mjs out/look` (each
region mid-key, with real contrast and more than one hue family, with a control that must fail), and the gates in
`tests/emberthrone-gates.json`.
