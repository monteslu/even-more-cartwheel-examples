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
- Lock-on: the player faces the target, moves in a circling walk blended from the sidesteps and the back walk, and the camera
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
  ring on the ground that fades in and grows to the blow's reach. The heroes' and bosses' blades leave a ribbon from the
  weapon's tip as the clip swings it, through exactly the ticks where the blow is live.
- Beating a whole group puts a sip back in every flask (up to its size): fights pay for the next one.
- Bosses: phases at two thirds and one third of their health. Each change is a roar (invulnerable, a hold, and a breath
  that pushes everyone near back out of reach), then quicker, newer blows: charges across the arena, shockwaves that
  run out along the ground. In the later phases a blow opens a fixed string (the Thornwarden's haymaker into a spin;
  the Tidecaller's thrusts into a wave; the Ember King's haymaker into a spin into the hammer), each with its full
  wind-up, and after a big blow or a string's last the boss stands spent for most of a second, breath steaming, taking
  twice the poise: a fair exam with an answer and an opening.

## The guard, the meter, the wardens' help, the trials

- The guard (held: the left trigger) turns a blow from in front: a quarter of its harm, the rest paid in stamina; an
  empty stamina breaks the guard and the blow lands. Pressed just as a blow arrives (a window of 0.2 s, then 0.55 s
  before the next) it parries: no harm, the attacker thrown off balance and wide open (the next blow is a critical), a
  bright ring of sparks and a sharp rumble. A warden parried also stands spent for a moment.
- The ember meter fills with blows landed (a cut 5, a heavy 8, a critical 12), a parry (22), a roll through a blow (8)
  and a quarter of the harm taken. Full, the right trigger spends it on the ember strike: an all-round blow that knocks
  everything down, with an insert on the blade hand and a hero angle as it lands (the director, below).
- In the King's fight a warden you spared can be called (with nothing to use near, the use button): it appears beside
  you, strikes the King once with its biggest blow and goes; again after 25 seconds.
- Boss trials (the pause menu, once a warden is beaten): every warden beaten so far, again, in a row, against the
  clock, healed between them. Beating one there gives nothing; a fall ends the trials; then the journey is exactly as it
  was. The best time is kept in the save.

## The story

The wanderer is the thief. A guide, hooded, waiting at the first shrine, sends you for the Sun Key and the Moon Key so
the gate of the Ember Keep can be opened and the King on his throne ended. The wardens tell another story as you take
the keys from them: they guarded a sleeping King who held the fire against the frost, and the guide is the frost's
voice. The world explains its own rules: the shrines are embers the King keeps lit, so a fall wakes you at the last one
and the foes come back with them; a warden stands spent after a big blow because its own fire is running out.

- Scenes (story/ember.story, compiled and voiced by tools/story.sh): the guide's welcome; each warden's intro when it
  wakes; after its last blow, an insert and a pull back, then its defeat and a choice: leave it breathing or take the
  key from a cold hand (a spared warden is the one you can call in the King's fight); the gate, once both keys open it
  (the guide's line depends on how merciful you were); the King's intro and defeat, where you choose the ending: take
  the throne and keep the fire, or give it to the guide; the ending told.
- Barks over the fights: each warden at a phase change (with a short react shot), when spent, when winning and when
  losing; the guide when you fall; the keepers at the shrines when you rest, once each and keyed to what happened (three
  falls to one warden, the keys, a second player).
- Every line is voiced (Kokoro-82M, generated ahead of time), subtitled at the readable size, skippable (East held),
  played on the pad alone, and never blocks play except in a scene. Faces speak: their jaws follow the voice and their
  brows and mouths show the line's feeling (morph targets added to the speaking figures by
  tools/chars/face-morphs.mjs). The colours and the light stay bright; the darkness is in the words.
- The cinematics are the engine's director (cartwheel/director.h): shots solved against the people every frame, so a
  close-up never ends on the back of a head; the camera never shakes (the pad rumbles).

## The camera

An orbit behind the player, part of the simulation (it decides what "up the stick" means, so the gates compare it).
It follows lazily behind the way the player runs when the camera stick is left alone. Locked on, it swings a little
off the line between the hero and the foe (the hero to the left, the foe clear past the right shoulder) and steps back
until both whole bodies fit with a margin (cartwheel/framing.h), easing in and out over a third of a second; the stick
still steers by the bearing to the foe. It pulls in instantly when a sphere swept from the player's head out to the
camera meets a wall (and eases back out), and never sinks under the ground. With two players it frames both.

## The world

One 640 m terrain (Terrain3D, 1 m samples) shaped from tables: zones (the meadow bowl, the canyon floor, the boss
rings, the marsh basin, the keep hill, the heights plateau and its gorge, the underway cutting, the downs round the
spire, the wood), ramps between them (stairs, switchbacks, a cliff path), highlands between the regions and mountains
round the edge. Painted by zone and slope, with soft edges where a path meets the grass; grass and stones scattered by
the engine. Trees are the game's own: nine species (oak, birch, aspen, willow, pine, fir, windswept, deepwood and the
glowcap), a few per region, each a trunk and a crown of alpha-cut leaf cards, drawn in 48 m tiles (one instanced mesh
per species, region and tile): full crowns that sway in the wind within 70 m, an impostor crown of a few large clusters
out to 190 m, nothing past that (the haze has them). The ground is eight CC0 photo-scanned layers (grass, snow, the
paths' packed earth and grit, the canyon's cracked sandstone, rock, the marsh's moss and mud, paving, the keep's burnt
earth), graded to each region's palette, read by the engine at a random offset in every small cell so no tile comes
round again, blended by height where two meet, and read from the side with the engine's detiling on cliffs and steep
banks; no uv turn or scale is painted per texel (the engine takes both from the nearest texel, and a change between
neighbours drew a straight seam). The keep's warm hill and paving carry cold teal moss in patches, its complementary
accent in its own ground whoever stands there. The canyon floor breaks up its sand with darker strata in thin broken bands
and darker stretches where the sand lies thin, and carries twice the stones of elsewhere (a second layer of them on its floor).
Pebbles lie on the paths, the canyon floor and the keep's hill (the game's own rock, as are the
larger stones). The stones grow on ground up to 28 degrees and never at the brink of a drop (a density map the game
makes from the terrain's tables at each start and writes to the project layer), and each has one box the shape of its rock to bump into. The meadow's moss is as
matte as the grass (roughness 0.9), so a glancing look never turns it pale. The grass is the engine's foliage (TerrainScatter3D GRASS, cartwheel/foliage.h): tufts of twelve curved tapering blades, seven a square metre, each tuft its own shade (dry yellow to deep green), rooted in the colour of the ground under it, lit the same from both sides, bending in the wind with gusts that run over the meadow, and bending aside round whoever walks through it (the players and the meadow's foes, standing up again over a second and a half behind them), each tuft wholly lit or wholly in shadow (one shadow sample at its root); out to 16 m, dissolving over the last 6 and cross-fading into the far level of detail (a dither the temporal antialiasing resolves). The near ground varies its brightness over 30 m features (macro tint), and the meadow carries 25 m swathes of drier yellow-olive and darker blue-green (a light share of the packed earth and of the moss). The layers meet by a softer height blend, the paving stands up in parallax within 12 m, and the keep's moss grows up through the paving's joints (a crisp height blend, never a whole texel). The picture is antialiased in time (TAA), which the grass's fades and edges need. A cloud layer drifts over the sky. Buildings are boxes
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
body and baked into their own atlas. tools/chars/make-characters.mjs writes each on the source rig's full skeleton
(three spine joints, the shoulders, every finger joint, the toes).

The motion is captured motion, not keyed poses: Quaternius' Universal Animation Library 1 and 2 (CC0), on the same rig.
tools/chars/blender/build_anims.py picks the game's clips (the sword stance, walk and jog, the three-cut combo with its
recoveries, the dive roll, the hits, the knock-down and get-up, the kneel at a shrine, the flask, the chest, the push,
the foes' chop, overhead, lunge, shield dash and hook) and makes the ones the libraries lack from their motion: the
sidesteps are the walk turned at the hips with the chest kept on the target, the back walk the walk run backwards, the
heavy and the bosses' spin the wide cut whipped round a full turn with the hips leading, the back step the library's
hop. Only a boss's roar is keyed, on the rig, with the joints staggered so the chest leads and the arms and head
follow. The packer retargets every clip onto each figure (its legs, its stance, its planted feet). In the game each
figure runs an AnimationTree: a 2D blend space of the stand, walk, run, back walk and both sidesteps by the velocity in
its own frame (each clip at its own ground speed, the cycles in step), every action a one-shot faded over it and timed
so the blow lands on the rules' hit frame, a blow playing out its follow-through while the body stands, leans into
turns, starts and stops, and the chest, neck and head turned toward what it fights: the locked foe, or free the
threat in front (a foe winding up at it first, else the nearest one on the hunt within 9 m). The sword stand's arms ride
over the walks while it fights, so a figure comes back to its guard out of a roll, a hit or a cut. The clips advance at the
end of the game's tick and the engine solves the skeleton modifiers again on that pose (cw_skeleton_modifiers_refresh), so
the look and the feet adjust the pose that is drawn, the same tick; a
figure turns at most half a radian a tick on screen. The Ember King's cape is cloth
on his chest bone that swings and settles. Hair moves with the body: the lancers' and the Tidecaller's long hair hangs on
five chains of three bones round the back and the sides, the wayfarer's two locks on two bones each (her buns stay tied),
the warden's and the Ember King's beards on one short chain at the chin, each chain found by
tools/chars/blender/build_chars.py down the middle of the hair's own shell and the hair weighted along it, the crown,
the fringe and the jaw line left on the head. In the game a SpringBone3D after the look swings them: the tips lag behind
the head on a turn, a start or a stop and settle back to where they hang, kept out of the skull, the jaw, the neck and
the tops of the shoulders (capsules the size of the figures' own skin), on the figures near the camera; a step up a
ledge moves them with the body instead of flinging them. The lancers' coats swing too: five chains of three bones round
the coat's sides and back, from a seam at the waist set clear of the thighs, linked across into one cloth (the engine's
skirt mode) that a stride spreads, flares on a stop and settles, with capsules on the thighs, the shins and the feet
keeping the legs inside it. The Tidecaller's robe is open down the front: a floor-length robe whose opening is about a sixth
of its round at the hem and a third at the waist, so a stride steps out through it instead of through the
cloth. Five chains of four bones round its sides and back swing it the same way (skirt mode, OPEN), damped harder than
the coat; its front panels go with the thighs; her leg capsules are 10 cm, between the shared 8.5 cm and the outfit's
fuller legs, so the cloth keeps off them without resting on a knee raised on a stair. On the closed robe it replaced the
outfit's legs came 18 to 44 cm out through the cloth over the hair test's run (a closed ring cannot let a stair-climbing
knee through); on the open one at most about 2 cm, where a raised knee meets the cloth below the waist. Feet are planted on slopes, stairs and stones at every pace (FootPlacement3D on the physics ground, each kind's
ankle height); the dead fall as ragdolls. Each
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
- `tests/emberthrone-motion.mjs` (romdev, the RELEASE cart): how the characters move, from their bones (view.c's motion
  recorder) through a pad script (run, stop, turn, a lock-on fight with sidesteps, a combo, a roll, a heavy, a back
  step, a back walk) and the bot at the Ember King's throne: the joints of a cut break one after another (their turning
  speeds weakly correlated, their peaks spread), no pop at a clip change, the King's cape swings and settles, the trunk
  leans into a turn, the stand is no sine loop, the head on the locked foe (10 degrees) and on the threat when free (15
  degrees), the hands back in the guard after a recovery (12 cm, in the chest's frame). Its control is the procedural build, which fails every check. It also
  writes a video and native-scale strips of each moment, and measures the frame cost.
- `tests/emberthrone-hair.mjs` (romdev, the RELEASE cart): the hair and the beards on their springs, from the bones
  (view.c's hair recorder) through a pad script (run, stop, a turn, a stop, a turn about, a stop) with the player
  wearing each figure that has them: the tips swing on each turn and each stop and settle within a second, nothing
  shakes at rest (at most 0.1 mm, or the figure's own idle jitter at the head where that is more: the clips are keyed at
  30 Hz and played linearly, so at a stand the body moves in two-tick straight pieces, 0.2 to 0.44 mm at the head by
  figure, and the hair hanging from it keeps 0.1 to 0.12 mm of that, a thirtieth of a pixel at the play camera; slack
  undamped chains, the control, shake 2 to 8 mm), and no chain goes
  into the skull, the jaw, the neck or the shoulders; the coat and the robe swing and settle the same way and no leg
  goes more than 25 mm into them. For the robe the legs are also measured as drawn: the figure's glb skinned from its
  recorded skeleton, and no part of the outfit's or the skin's legs more than 25 mm out through the cloth (a leg out
  through the opening or under the hem is not through it; tests/lib/robe-legs.mjs). Controls: the old rig (the
  springs off, the hair carried by the head), the springs without collisions, slack undamped chains and the garments'
  chains left unlinked, each failing its check, and the robe's old rig failing the legs; the closed robe before it, run
  as an old build, fails the legs too. It writes native-scale strips of each turn and stop round the head and the body.
- `tests/emberthrone-flash.mjs`: no strobing, on the engine's flash check (WCAG 2.3.1 and the Harding thresholds,
  every frame). Two runs: eight seconds of the bot's journey, and three and a half seconds of the Tidecaller's fight from
  just before its busiest moment (its phase roar). Controls: each under the engine's 8 Hz strobe, and the fight under
  the game's own 10 Hz white wash, must fail.
- `tests/emberthrone-gates.json`: the bot's first 30 seconds (it rests, crosses the hollow, fights the raiders on the road
  and reaches the canyon shrine) and a scripted pad run (Start, Start, the road, the lock, three cuts, a roll, a heavy, a
  drink) with its control. Both agree to the bit across native GL, native WebGPU and the cart on romdev's WebGPU host.
- `tests/emberthrone-soak.mjs`: the whole game played by the bot on software GL, 30 rule ticks a frame: it wins (18.5
  minutes of game time through all eight regions: 50 foes, all three bosses, both keys, the maul, every shrine, no
  falls), positions stay finite, the camera never has a wall between it and the player, the route never stalls 6
  minutes, memory holds (1 percent). Control: an empty maul chest (the cracked wall never breaks) must fail. The two
  halves run as two suites (`--no-control`, `--control-only`) to keep each hold of the GPU suite lock short.
- `tests/emberthrone-ground.mjs` (frames from `tests/emberthrone-ground-shots.mjs`: the auto cart on romdev, its
  `groundcam` holding the camera straight down over each region's open ground): no seam (no pixel boundary where the
  step across it is the hardest of its neighbours along most of the frame) and no repeat (the autocorrelation of a
  45 m view, its 2 m mean off, under 0.25 at every lag from 1.5 to 15 m), in all eight regions; every layer texture
  tiles (the step across its wrap no more than 1.25 times the step inside). Controls: the 0.49.0 ground (a uv turn per
  4 m block drew a grid of seams in every region), its layers laid plainly (a repeat the measure must see) and its
  layer textures (three did not tile) must fail.
- `tests/emberthrone-grass-walk.mjs`: the hero walks off the road into the meadow on a pad (one romdev ranged call, GL
  and WebGPU); the grass's flicker over the ground's is at most 1.2 times the 0.51.0 grass's on the same ground and walk,
  on 2% or more of the picture, its one-frame pops reported. Control: the grass cut off hard at 9 m
  (`ADV_PROBE_GRASSPOP`) must fail.
- `tests/emberthrone-grounding.mjs`: 106 props, 546 building boxes and 4460 scattered stones and pebbles on the ground (worst 0 cm).
- `tests/emberthrone-lockon.mjs` (romdev, the motion pad script, view.c's framing recorder): locked on, the foe whole on
  the screen, the hero over a quarter of its box in 5% of the frames at most, the hero at most 40% of the frame's height
  and the foe at least 12%. Control: 0.51.0's camera.
- `tests/emberthrone-trail.mjs` (romdev, the motion pad script, view.c's trail recorder): the blade ribbon's head within
  6 cm of the sword's tip on average (from the skeleton as posed). Control: 0.51.0's ribbon on the rules' arc.
- `tests/emberthrone-stones.mjs` (romdev, world.c's `stones` command): rays down and in from the sides land on each
  stone's rock, no stone under a camera crown, none on ground steeper than 35 degrees or standing out over a drop.
  Control: 0.51.0's stones (a tree's trunk and crown, slopes to 70 degrees).
- `tests/emberthrone-moss.mjs` (romdev, world.c's `moss` command and `groundcam`): the meadow's moss from a standing eye
  at a glancing angle keeps half its straight-down saturation or more. Control: 0.51.0's glossy moss (0.55).
  Control: one tree lifted half a metre must be caught.
- `tests/emberthrone-look.mjs` (frames from tools/capture-look.sh, five per region spread over its fight, judged on
  their median: one frame is one moment of a fight): each region's play is mid-key (mean luma 100
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
