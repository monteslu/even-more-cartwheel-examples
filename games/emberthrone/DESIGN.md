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
- The first cut of a combo (and a heavy) steps up to a metre toward the locked foe, or the nearest one in front with no
  lock, spread over its wind-up: a foe that sidesteps the wind-up is still met.
- Every blow that lands has weight. The whole world holds still for a few ticks (both bodies, the camera, every clip):
  4 for a cut, 5 for the third, 6 for the maul's swing or the heavy, 8 for the maul's slam, a broken poise or a
  knockdown, 9 for a critical, 10 when a boss turns to its next phase, 6 for a blow taken (8 from a boss). Presses made
  in a hold are kept for the tick after. The push follows the blow (a cut nudges, the third cut and the maul throw) and
  the body's weight (brutes and bosses barely move). Sparks, a sound, a small warm light that swells and fades over a
  third of a second where it landed, and pad rumble by how hard it was all come together. The camera never shakes.
- Poise: light blows wear it down; when it breaks the foe stumbles for most of a second with a ring of sparks, and the
  next blow in that opening is a critical (double damage, a longer hold, a heavier rumble). A knockdown opens the same.
- The dodge is a dive roll with invulnerable frames through most of it (2 to 21 of 37 ticks); with no direction, a
  quick back step (1 to 13 of 25). Rolling through a shockwave is the answer to the bosses' later phases.
- Stamina pays for every swing and roll and comes back after a short wait, so a fight has a rhythm.
- Lock-on: the player faces the target, moves in a circling walk with sidestep and back-step clips, and the camera
  turns to keep both in view; a flick of the camera stick moves to the next foe that side.
- Foes, each kind with one idea: raiders (quick cleavers, a slower overhead), brutes (hyper-armoured through their
  swings, a slam with a ring on the ground, a looping haymaker), lancers (long reach, they keep their distance). Roles
  on top of a kind give a group its shape:
  - the shield bearer holds a round shield up in front: a cut from the front glances off (the striker bounces back)
    and wears the shield down; a heavy or the maul knocks it aside at once, and so do a few cuts; from the side or the
    back it is open. It turns quickly and shoves. Go round it or break it.
  - the slinger keeps nine metres off and throws stones straight at where you stand: standing still is hit, moving is
    not. It backs away when you close in.
  - the marsh lurker waits under the pools and rises when you wade close: nothing can lock on to it until then.
  A group wakes together; two at a time come at each player (an attack token) while the rest circle round toward the
  player's back, so a group closes from more than one side. Every blow has a wind-up of 26 frames or more, with a warm
  light on the foe that swells through it and fades after it, and a sound (lower for the big blows); area blows draw a
  ring on the ground that fades in and grows to the blow's reach. The heroes' and bosses' blades leave a ribbon through
  exactly the arc where the blow is live.
- Beating a whole group puts a sip back in every flask (up to its size): fights pay for the next one.
- Bosses: phases at two thirds and one third of their health. Each change is a roar (invulnerable, a hold, and a breath
  that pushes everyone near back out of reach), then quicker, newer blows: charges across the arena, shockwaves that
  run out along the ground. In the later phases a blow opens a fixed string (the Thornwarden's haymaker into a spin;
  the Tidecaller's thrusts into a wave; the Ember King's haymaker into a spin into the hammer), each with its full
  wind-up, and after a big blow or a string's last the boss stands spent for most of a second, breath steaming, taking
  twice the poise: a fair exam with an answer and an opening.

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

A stylised cast read from the third-person camera: silhouette first (the heroes slim and upright, the brutes square
under a crested helm and heavy plate, the lancers long verticals in open coats with a spear, each boss crowned by its
own shape: the Thornwarden's horns and thorns, the Tidecaller's shell fins over a floor-length robe, the Ember King's
crown and cape), then value and hue (the heroes bright, teal and crimson; the foes darker, in their region's colours;
glowing eyes on every foe). Faces are hand-painted with separate eyes and light catches, a little larger than life,
like the hands, so they read at play distance. Cloth is matte, leather a thin coat, plate three-quarters metal with
bright worn edges painted in, skin moderate with soft wrap shading.

Built from CC0 bases (Quaternius' Universal Base Characters and Modular Character Outfits - Fantasy; see CREDITS) by
tools/chars/blender (headless Blender): the outfit and a base head and hair, posed from the T pose to the game's rest,
sized, bulked bone by bone (the brutes and bosses), recoloured, the skin of head and hands baked into one atlas, and
the generated pieces (plate, helmets, crowns, horns, robes, coats, capes, masks, bracers) lofted round the measured
body and baked into their own atlas. tools/chars/make-characters.mjs puts each on the game's skeleton (the joints from
the source rig) with the game's clips: the sword idle and run, the three cuts, the heavy spin, the dive roll and back
step, the sidesteps and back walk, the kneel and rest at a shrine, the drink, the push, the roar. Feet are planted on
slopes and stairs (FootPlacement3D on the physics ground, each kind's ankle height); the dead fall as ragdolls. Each
figure draws its coarser level of detail when it is small on the screen (cwpack's simplified meshes). Foes take their
region's colours (the outfit's material tinted per kind and region).

## Tests

- `tests/emberthrone-rules.mjs` (games/emberthrone/rules.c, natively on software GL, nothing drawn): the combat feel on
  the real rules and world. The hold of a landed cut (4 ticks, both bodies still, a press in it kept) and its ladder by
  blow, knockback by blow, the critical after a broken poise, the cut's magnet (a locked foe 3.4 m off is met), the
  shield's guard (front blocked, back open, a heavy breaks it), the slinger's stones (half or more hit a player standing
  still, a quarter or fewer one who keeps moving), waiting foes circling to the player's back (half or more of the blows
  come from beyond the shoulder), a boss spent after a big blow and chaining blows in its later phase, the flask's sip
  for a beaten group, the lurker, rumble on a hit. Controls: each rule off in turn, and its own check must fail.
- `tests/emberthrone-flash.mjs`: no strobing. Two seconds of the Tidecaller's fight from its busiest moment, frame by
  frame, against WCAG 2.3.1 and the Harding thresholds: no ninth of the screen may flash at all, no 1/144 of it more
  than 3 times (general or red) in a second. Control: the same frames under a 10 Hz white wash must fail.
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
  shading to 85 m. The terrain is most of the frame: a texel that blends two layers reads both, so second layers are
  patches with a narrow blended rim (the wood's moss alone was 1.2 ms on GL, 2.2 on WebGPU as a wide soft blend) and
  path edges are narrow; foes are drawn within 45 m and cast the sun's shadow within 24 m. The meadow (about 4.8 ms,
  the most open view of the most painted ground) and, by a hair, the wood and the spire on WebGPU are still over the
  bound. The probe moves the terrain node to take the terrain out (Terrain3D ignores `visible`).
