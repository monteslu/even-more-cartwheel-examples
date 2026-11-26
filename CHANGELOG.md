# Changelog

## 0.61.0

- **Every game** moves to cartwheel-engine 0.84.0. Beat Sync's program table gains one state the engine's variant
  closure now asks for (a double-sided material over the low water); every other table regenerates unchanged. No game
  here uses game-tree search, title items, the recorded pointer channel or mesh opacity, all opt-in; the scatter
  crossfade and the matte GRASS tuft touch Ember Throne alone.
- **Pinball**: the table select changes table at once. A change used to rebuild the whole table in one frame, freezing
  the screen for one and a half to two and a half seconds per press while presses piled up; every table is now built
  behind the loading screen and a change only switches which one is shown. A chooser at the foot of the select names
  the table (TABLE n OF 3) between two arrows with the d-pad and flipper glyphs of the device in hand, and its arrows
  and play line take taps and clicks. D-pad down no longer starts a game on the select (it counts the players there).
  Check: `tests/pinball-table-select.mjs` on GL and WebGPU picks and plays all three tables on the pad alone; its
  control, the 0.60.0 cart, fails it.

## 0.60.0

- **Every game** moves to cartwheel-engine 0.83.1 (no program table changed; cart smoke, version, art and prompt checks
  pass on all nine).
- **Ember Throne** gets a voiced story with choices: the wanderer wakes to a guide who sends them for the wardens' keys,
  and the wardens were guarding something else entirely. 73 voiced, subtitled, skippable lines (engine story and speech
  modules), shrine keepers whose lines react to what happened (falls at a warden, keys held, a second player), a choice
  over each beaten warden to spare or take its fire, and two endings. Faces speak: jaw, mouth, brows and expressions move
  with each line on the wanderer, the wardens, the bosses and the guide. Boss fights open on a cinematic intro, phase
  changes cut to a short react shot, and a finisher plays an insert and a slow pull back; the camera never shakes.
- **Ember Throne** combat: a guard that takes a quarter of a blow and parries one pressed as it lands, an ember meter
  spent on an all-round strike (right trigger; guard is the left trigger), a spared warden you can call into the King's
  fight, and boss trials from the pause menu.
- Checks: the rules test (parry, block, meter, special, assist, trials, each with a control that fails), gates
  emberthrone-story, -story-skip, -reactive and -boss-intro on GL and WebGPU with controls, emberthrone-journey with its
  pins unchanged.
- **Ember Throne**'s lancers' coats swing with the body. Each coat hangs on five chains of three bones round its sides
  and back, from a seam at the waist set clear of the thighs; the cloth and its hem are skinned onto the chains, which
  the engine links across into one open cloth (skirt mode) that a stride spreads. Capsules on the thighs, the shins and
  the feet keep the legs inside it; the cloth takes eight passes a tick (with four, a panel came 70 mm into a thigh on a
  turn about). On the pad script (run, stop, turn right, stop, turn about, stop) the coat flares 0.40 to 0.58 m on a
  stop and falls under a tenth of that in 32 to 51 ticks; at rest it shakes by 0.24 mm or less (root mean square), and
  a leg comes at most 1.4 mm into it.
  tests/emberthrone-hair.mjs covers it, with native-scale strips of the whole figure, and a new control: the coat's
  chains on their own, not linked (legs 90 mm into the coat). The Tidecaller's robe keeps its 0.57.0 rig: on its
  springs the outfit's legs came through it and its cloth creased between the chains.
- Checks on this change, on cartwheel-engine 0.78.2 with a Release cart: the hair test on all five figures with its
  controls, native-scale strips of the lancer's whole figure through each turn and stop, and cart smoke.

## 0.59.0

- Every game on cartwheel-engine 0.78.2, every program table generated again for it, now with each material's
  variants (every environment and preset it can switch into), so no state is left to draw without a program.
- **Ember Throne**'s terrain asks for the rotated and scaled ground blend by name (`uv_blend`), which the engine no
  longer turns on by itself.

## 0.58.0

- Every game on cartwheel-engine 0.77.0, every program table generated again for it.
- **skate**'s hair swings. The ponytail, a fall of hair down the back, the headband's tails and a cap's tufts lag
  behind a pivot, a carve, a pop or a landing, sway and settle back to their hang, kept out of the head and the upper
  back (cartwheel-engine's SpringBone3D on the two hair bones, which the rider's own pose now leaves to it).
- The pause menu has the game's own items while you play: **skate** Restart run and Change park (back to the setup),
  **towers** Restart map and Choose map, **destructo** and **detexto** Restart structure and Choose structure (back
  to the select screen; the shots so far are not judged or saved).
- **skate**'s control hints of a run's first seconds are no longer drawn under the pause menu, and **detexto**'s pause
  plate reaches the prompt line, so the longer menu stays on it.
- Checks on these four games, on cartwheel-engine 0.77.0 with Release carts: skate's and towers' gates on GL and
  WebGPU; destructo's play, controls and newcomer gates; detexto's one-bit menus at 100% and 200% text with their
  controls; cart smoke, version and prompt checks. Each pause item was played through on romdev with the pad (restart,
  then back to the choice). destructo's loading and water gates time the CPU and went over on a machine under a load
  of about 50; the build before this change goes over the same way there.
- **Ember Throne**'s canyon floor is broken up by its own art: darker strata cropping out in thin broken bands across
  the sand, darker stretches where the sand lies thin, and twice the stones of elsewhere (a second layer of the same
  stones on the canyon floor alone, so every other region places its stones as before). The canyon's luma spread is
  55.9 over its five look frames against the look check's 55 (0.53.0: 54.0).
- **Ember Throne**'s grass bends aside round the players and the meadow's foes as they walk through it and stands up
  again over a second and a half behind them (cartwheel-engine foliage movers, `bend` 1.1). Each tuft is wholly lit or
  wholly in shadow (`shadow_at_root`), and the rim of a figure's shadow in the grass no longer carries a fine dot
  pattern in a run: at the same framing it has 19.5 isolated dots a thousand pixels, against 44.1 on cartwheel-engine
  0.76.0's screen-door fade.
- **Ember Throne**'s clips advance at the end of the game's tick and the engine solves the look and the feet again on
  that pose the same tick (`cw_skeleton_modifiers_refresh`), in place of a tree hook before the modifiers. The head
  stays on the locked foe within 4.2 degrees on average and on the threat within 11.3 when free (were 4.9 and 13.8).
- **Ember Throne**'s feet are planted at every pace, a run's included (they were let go above a walk). In the motion
  check's run no leg bone turns more than 16 degrees in one tick beyond its neighbours (38 with the engine's turn limit
  off, `CW_TEST_FOOT_NO_RATE=1`).
- **Ember Throne** no longer draws every figure for one frame in its rest pose after a jump ahead or a respawn: a
  figure made on a load poses its first tick.
- **Ember Throne**'s stone density maps are made from the terrain's tables at each start and written for the engine to
  read (they were packed images). tests/emberthrone-stones.mjs's control is now the same cart with the map never
  written, which must fail.
- **Ember Throne**'s grass shrinks away at the edge of its range and at its level of detail change, for now (`fade_mode`
  SHRINK): on cartwheel-engine 0.77.0 the dithered cross-fade draws both levels of a tuft whole through the band, two
  surfaces in one plane that fight for the depth test (150 pairs in the world). The grass goes back to the dither with
  the engine release whose cross-fade draws no coincident levels. In motion the band changes no more frame to frame
  than on 0.76.0, and the grass costs the same.
- Ember Throne's motion check takes `--env` for switches the cart reads (the engine's controls among them).
- Checks on Ember Throne, on cartwheel-engine 0.77.0 with Release carts: gates on GL and WebGPU, grounding, flash,
  perf (16 of 16 under 4.5 ms; its controls fail), the look of all eight regions at five frames each, the motion
  check against the procedural build (which fails), the depth-fighting scan (no pairs; its control and selftest fail as
  they must), cart smoke and version.

## 0.57.0

- **Ember Throne**'s hair and beards move with the body. The lancers' and the Tidecaller's long hair hangs on five
  chains of three bones round the back and the sides, the wayfarer's two locks on two bones each (her buns stay tied),
  and the warden's and the Ember King's beards on one short chain at the chin. The character build finds each chain
  down the middle of the hair's own shell and weights the hair along it; the crown, the fringe and the jaw line stay on
  the head. In the game a SpringBone3D after the head's look swings them. The tips lag behind the head on a turn, a
  start or a stop, then settle back to where they hang. They are kept out of the skull, the jaw, the neck and the tops
  of the shoulders by capsules the size of each figure's own skin. Measured on the pad script (run, stop, turn right,
  stop, turn about, stop): the long hair swings 16 to 28 cm on a turn and settles 35 to 42 ticks after a stop, and the
  locks and beards swing 4 to 9 cm and settle within 30 ticks. At rest it shakes by about 0.2 mm at most, and no joint goes
  more than 1 mm into the body (the old rig, hair carried by the head, put the lancer's hair 44 mm into his shoulders).
  A 0.41 m step up a ledge in one tick moves the hair with the body instead of flinging it.
- **Ember Throne**'s robe and coats have their bone chains in the rig, from the waist: eight of four bones round the
  Tidecaller's robe, five of three round the coats. They carry no weight yet, and the cloth stays on the legs as built
  until their springs settle on these figures.
- tests/emberthrone-hair.mjs: each figure's hair or beard on the play camera through a pad script in one romdev call,
  the player wearing each figure in turn. The turn swings, the settle after each stop, the jitter at rest, the deepest
  point in the body, and native-scale strips round the head. Controls, each failing its own check: the old rig (the
  springs off), the springs without collisions, and slack undamped chains. A beard never reaches the neck even
  without collisions, which the check reports. The springs cost under half a microsecond a figure over its other
  skeleton modifiers (13 us a figure on the Steam Machine).
- **jewels**: the pause menu has New board, a fresh board with the score from nothing (the best is kept), on the pad
  or by a tap. The first cascade of a game no longer stalls a frame on WebGPU: the shockwave's programs are made on
  the first frame the effects pass runs (`cw.pass_builds` holds flat in play, 48 from the title on, where it grew from
  46 to 48 at the first big clear), and every glyph the panel and the gains can show is made under the loading screen
  (the cascade counter's first letters cost about 35 ms of CPU in that frame). New gate `jewels-newboard`, with
  Resume as its control.
- **pinball**: the pause menu in a game has Restart game (the same table and players from the first ball) and Change
  table (back to the table select); a game left either way keeps no score. New gates `pinball-restart` and
  `pinball-change-table`, with Resume as their control.
- **lanternfall**: Settings has the game's own row, Leaves lost: every blow (the default), every second blow or every
  third blow costs a leaf of health, saved with the settings and applied at once, in play too. New gate
  `lanternfall-assist` (debug field `game.assist`), stepping the row the other way as its control.
- Checks on these, on cartwheel-engine 0.76.0 with Release carts: every gate of jewels, pinball and lanternfall on GL and
  WebGPU, natively and in the cart on romdev, the new ones with their controls; jewels' flash check on GL and WebGPU
  (its two strobe controls fail); the perf gates of the three games on a Radeon RX 7600 (jewels 1.87 ms GL and 1.90
  WebGPU, bound 4.2; pinball 2.18 and 1.38, bound 4.1; lanternfall 2.37 and 2.42, bound 4); jewels' play frames the
  same as before to the pixel; cart smoke, version and prompt checks.

## 0.55.0

- Every game on cartwheel-engine 0.76.0, every program table generated again for it.
- **beatsync**'s fades end cleanly. On 0.76.0 a fade's end frames cost what the fade itself does: the arcade maze into
  one bit no longer has two frames 2.6 ms over at its end, and the metal hall into the rock stage, the lava into the
  metal hall and the half blocks into the metal hall have no end-frame spike. So the arcade maze and one bit fade into
  each other again, and the GL fade bound in the hitch check is held again.
- **beatsync**'s hitch check holds that no engine pass (post, looks or backdrops) is built in play after the warm-up,
  read from the engine's `cw.pass_builds`. Its control, the cart with no prewarm, must build passes in its fades, and
  it does (26 on GL and 39 on WebGPU).
- Known issues:
  - every twelfth frame of a style whose world lights the field costs about 5 ms more GPU on the 890M (the
    environment's filter makes a cube of at least 256 texels a face for a 64-texel capture), an engine cost;
  - on WebGPU an occasional fade frame reaches 8 to 11 ms on a Radeon RX 7600 (in 890M units). It lands somewhere
    else in each run, from pauses of the host, and is neither the game nor the engine, so the WebGPU fade bound is
    printed rather than failed. Every frame of the tour stays under 16.7 ms on GL and WebGPU.
- Checks on this release, on cartwheel-engine 0.76.0 with Release carts, on a Radeon RX 7600 (GPU times in 890M units):
  - beatsync's hitch check on GL and WebGPU: 30 style changes, the arcade maze into one bit among them, with no frame
    over 16.7 ms. The worst GL fade frame was 5.8 ms, under the 8 ms bound. Its control, a cart with no prewarm, must go
    over, and it does.
  - beatsync's fade ends, each fade run whole: the arcade maze into one bit, the half blocks and the lava into the metal
    hall, the night sea into the grid, and the metal hall into the rock stage. Each fade's end frames cost what the
    fade does.
  - beatsync's readability (4:1) in every style, each with its control, and the one-bit HUD in ink and paper, with its
    control.
  - beatsync's style fades against the flash rules, with the one-frame cut control; the song-length flash check, with
    its strobe controls; and the note footprint, with the old-shapes control. All three ran on this release's own
    build.
  - The program table at every preset, with its control.
  - Every game's cart smoke, version and prompt checks.
## 0.54.0

- **beatsync**'s arcade maze is back in the readability gate. The engine fixed the half blocks that drew a flat colour
  as a checkerboard of two palette entries (cartwheel-engine 0.71.1), and the maze reads 7.88:1 on GL and WebGPU.
- **beatsync** asks the engine to prewarm the glass roads' sharp reflections behind the loading screen
  (`cw_post_prewarm` with ssr and ssr_backdrop on), so the first style that turns them on in play builds nothing.
- **beatsync**'s style cost table was checked again with the new `tests/beatsync-cost.mjs`. Every style was held for
  180 frames at High on a Radeon RX 7600 (GL and WebGPU), and the means were fitted to the table's Radeon 890M figures
  over the 17 other styles (0.3 ms rms). One bit is 4.4 ms (4.3 on WebGPU), lava 1.5 and metal 3.0, so the table
  stands. The earlier 8.1 ms for one bit and 5.05 ms for lava into metal were readings of an 890M at a low clock. Lava
  and metal held half way cost 4.0 ms (3.4 on WebGPU), under their sum less the field.
- **beatsync**'s hitch check lists every fade frame over its 8 ms bound, with its frame in the run, the fade and how far
  into it, and keeps the run's per-frame series in `out/beatsync-hitch/`.
- Known engine issues on cartwheel-engine 0.71.2, held until the engine release that fixes them:
  - a world that lights the field is captured whole on the frame its environment comes on at a fade's end;
  - an engine look leaving at a fade's end costs one or two frames over the frames round it (the arcade maze into one
    bit: two frames 2.6 ms over), so the scheduler still keeps that pair apart;
  - every twelfth frame of a style whose world lights the field costs about 5 ms more GPU on the 890M (the
    environment's filter makes a cube of at least 256 texels a face for a 64-texel capture).
  The hitch check's fade bound stays held. Every frame of its tour stays under 16.7 ms on GL and WebGPU.
- On WebGPU an occasional fade frame reaches 8 to 11 ms on a Radeon RX 7600 (in 890M units). It lands somewhere else in
  each run, from pauses of the host, and is not the game's or the engine's.
- Checks on this release, on cartwheel-engine 0.71.2 with Release carts, on a Radeon RX 7600 (GPU times in 890M units):
  - beatsync's hitch check on GL and WebGPU: 32 style changes and no frame over 16.7 ms; the worst GL fade frame
    was 5.9 ms. Its control, a cart with no prewarm, must go over, and it does.
  - beatsync's readability (4:1) in every style, each with its control, and the one-bit HUD in ink and paper, with its
    control.
  - beatsync's style fades against the flash rules, with the one-frame cut control.
  - The song-length flash check, with its strobe controls.
  - The note footprint, with the old-shapes control.
  - The program table at every preset, with its control.
  - Every game's cart smoke, version and prompt checks.

## 0.53.0

- Every game on cartwheel-engine 0.71.2, with every program table regenerated for its shaders.
- **Ember Throne**'s grass is the engine's foliage. Each tuft has twelve curved, tapering blades, and there are seven
  tufts a square metre (there were two). Each tuft takes its own shade, from dry yellow to deep green, and is rooted in
  the colour of the ground under it. The blades are lit the same from both sides and bend in the wind, with gusts that
  travel over the meadow. Out to 16 m, the tufts dissolve over their last 6 m and cross-fade into their far level of
  detail, so none pops or shrinks. The picture is now antialiased in time (TAA), which the grass's fades and edges are
  made for; under FXAA the dense blades shimmered.
- **Ember Throne**'s meadow is no longer one flat green. The near ground's brightness varies over 30 m features, and
  25 m swathes run drier yellow-olive or darker blue-green. The grass layer and the tufts are a little less saturated.
- **Ember Throne**'s ground: the layers meet through a softer height blend, so the speckled rims are gone, and the
  paving stands up in parallax within 12 m. The keep's moss grows up through the paving's joints
  and fades at its edges; it was solid green patches. The keep's light fills the shade of its walls, so the lock-on
  fights' frames keep the look's mean brightness without washing out the sunlit paving.
- tests/emberthrone-grass-walk.mjs: the hero walks off the road into the meadow on a pad, in one romdev call, on GL and
  WebGPU. The grass's flicker over the ground is held to the 0.51.0 grass's on the same ground and walk: at most 1.2 times
  it (GL 4.82 against 4.18), over 6.7% of the picture where the old grass covered 0.9%. One-frame pops are reported.
  Control: the grass cut off hard at 9 m must fail, and does (5.89). This is a changed bar: a fixed bound of 4 no
  longer fits, because the meadow's new variation raises every run's figure, the old grass's included.
- Known look miss: **Ember Throne**'s canyon reads a luma spread of 54.5 against the look check's 55. The lock-on
  fights frame a wide, even sand floor. 0.52.0 passed on only 3 frames, so this is not shown to be a regression; the
  canyon's own art will break up that floor.
- tests/emberthrone-tufts.mjs is retired. Its slow camera walk can no longer tell the hard-cut grass from this grass,
  so its control passed. tests/emberthrone-grass-walk.mjs replaces it.

## 0.52.0

- **Ember Throne**'s lock-on camera steps back and to the side. It swings a little off the line between the hero and the
  foe, with the hero on the left and the foe clear past the right shoulder, and pulls back until both whole bodies fit
  with a margin (the engine's exact framing), easing in and out over a third of a second. Up the stick still walks at
  the foe. Through the motion check's fight, the hero now covers a quarter or more of the foe in 5 of 530 frames (it
  was 518), and fills 31% of the frame's height on average (it was 56%).
- **Ember Throne**'s figures turn their heads. The clips now advance in the engine's node phase, before the skeleton
  modifiers, so the look and the foot placement shape the pose that is drawn. Before, the clips wrote over every solve,
  and the head never followed what it fought. Locked on, the head is 5 degrees off the foe on average (it was 15). Free,
  it looks at the threat in front: a foe winding up at it first, else the nearest foe on the hunt within 9 m. A roll
  takes the head with the body. Foot placement runs at a stand and a walk, and fades out in a run.
- **Ember Throne**'s heroes come back to their guard. The sword stand's arms ride over the walks while a figure fights,
  so after a roll, a hit or a cut the hands return to the guard (10 cm from it after a recovery, where they were 21 cm).
  Running free still swings the arms.
- **Ember Throne**'s blade ribbon starts at the tip of the sword in the hand, as the clip swings it, through the ticks
  where the blow is live. It used to start on an arc of the rules round the body, about 2 m from the blade.
- **Ember Throne**'s meadow moss stays green at a glancing angle. It is as matte as the grass (roughness 0.9, was 0.55),
  so the sky's sheen no longer turns it pale like snow.
- **Ember Throne**'s stones fit their rock. Each one has a single box the shape of the rock, in place of a tree's 3 m
  trunk and a crown that stopped the camera. Stones grow on ground up to 28 degrees (it was 70, which put them on cliff
  faces) and never at the brink of a drop: a density map the game makes from the terrain's tables, shipped as
  textures/terrain/stones_density.png and checked at every start. 455 stones remain, and every scattered stone and
  pebble is on the ground (worst 0 cm).
- New checks, each with a control that must fail. tests/emberthrone-lockon.mjs: the foe is whole on the screen and
  clear of the hero; control: the 0.51.0 camera. tests/emberthrone-trail.mjs: the ribbon is within 6 cm of the sword's
  tip; control: the 0.51.0 ribbon. tests/emberthrone-stones.mjs: the bodies fit the rock, no stone is on steep ground
  or over a drop; control: the 0.51.0 stones. tests/emberthrone-moss.mjs: the moss keeps its saturation at a glance;
  control: the 0.51.0 moss. tests/emberthrone-motion.mjs now also measures the head's aim, locked and free, and the
  hands' return to the guard; control: 0.51.0.

## 0.51.0

- **Ember Throne**'s ground, in all eight regions. The grid of straight seams is gone: every terrain texel took a
  random uv turn in 4 m blocks, and the engine reads the turn from the nearest texel without blending, so each block
  edge drew a line across the ground; the turn is now 0 everywhere (the engine's own stochastic tiling breaks the
  repeat). The eight layers are CC0 photo-scanned PBR sets from Poly Haven and ambientCG (grass, snow, the paths'
  packed earth and gravel, the canyon's cracked sandstone, rock, the marsh's moss and mud, paving, the keep's burnt
  earth; see the game's CREDITS), graded to each region's palette by tools/make-ground.mjs: any faint step at their
  edges healed, their tile-sized patches taken out, height stretched for the engine's height blend, roughness set per
  ground (no glossy lawns), 1024 square so nothing is resampled; three of the old generated layers did not tile at
  their edges. The cliffs and steep banks use the engine's side detiling. The larger stones are the game's own rock,
  and pebbles lie on the paths, the canyon floor and the keep's hill (all on the ground: 5 719 scattered stones and
  pebbles, worst 0 cm).
- **Ember Throne**'s grass tufts are the game's own blade mesh: six curved blades a tuft, each its own height, lean
  and shade, dark at the root in the grass's colour so the base melts into the ground, normals bent up so they light
  like the ground, both faces drawn with the same normals (the engine's built-in tuft was five flat spikes with a pale
  tip, half of them lit from below); they vary in size and turn, and shrink away over their last 10 m to 15 m, where a
  blade would be thinner than a pixel and shimmer.
- **Ember Throne**'s keep carries its complementary accent in its own art, whoever stands in it: cold teal moss in
  patches over the warm ash of its hill, and in its paving (small patches whose rim the height blend fills from the
  joints first); the paving a little lighter. With no figures on screen (`ADV_PROBE_NOCHARS`) the keep's
  five frames read a 5.1% accent (4% bar) at a mean luma of 108.
- tests/emberthrone-ground.mjs (frames from tests/emberthrone-ground-shots.mjs: the auto cart on romdev, its new
  `groundcam` debug command holding the camera over each region's ground): no seam and no repeat in any region, and
  every layer tiles. Controls: the 0.49.0 ground (seam lines in every region), its layers laid plainly (a repeat the
  measure must see) and its layer textures must fail.
- tests/emberthrone-tufts.mjs: the camera walks slowly over the meadow and the spire's downs on GL and WebGPU; the
  tufts' frame-to-frame flicker over the ground's own stays under 4 of 255. Control: the 0.49.0 tufts
  (`ADV_PROBE_OLDTUFTS`) must fail.
- Checks on this change, on cartwheel-engine 0.70.1: Ember Throne's cart builds; its smoke, version and prompt checks;
  grounding; the ground check (seam peaks 0.38 to 0.43 in the eight regions against 0.73 to 0.93 before), the tuft
  check on GL and WebGPU (flicker excess -11.0 and -14.0 against 15.8 to 22.0 for the old tufts), the look check in
  all eight regions and in the keep with no figures, and the flash check. On 0.67.0, before the pin moved: its rules,
  gates and soak. The perf gate measured on another GPU: every place well inside its bound, 0.05 to 0.17 ms over the
  0.50.0 ground.

## 0.50.0

- **Ember Throne**'s characters move on captured motion instead of keyed poses and sine loops. The clips come from
  Quaternius' Universal Animation Library 1 and 2 (CC0), on the same rig the cast is built from: the sword stance,
  walk and jog, the three-cut combo with its recoveries, the dive roll, the hits, the knock-down and get-up, the kneel
  at a shrine, the flask, the chest, the push, and the foes' chop, overhead, lunge, shield dash and hook. The clips
  the libraries lack are made from their motion (tools/chars/blender/build_anims.py): the sidesteps are the walk
  turned at the hips with the chest kept forward, the back walk the walk run backwards, the heavy (and the bosses'
  spin and lariat) the wide cut whipped round a full turn with the hips leading, the back step the library's hop; only
  a boss's roar is keyed, its joints staggered. The packer retargets them onto each figure (`.cwimport`: its legs,
  its stance, its planted feet), each figure carrying only the clips its kind plays.
- Every figure is on the source rig's full skeleton (three spine joints, the shoulders, every finger joint, the toes)
  instead of the old 23-bone one.
- Each figure runs an AnimationTree: a 2D blend space of the stand, walk, run, back walk and both sidesteps by its
  velocity in its own frame (each clip at its own ground speed, eased through starts, stops and turns, the cycles in
  step), every action a one-shot faded over it and timed so the blow still lands on the rules' hit frame, a blow
  playing out its follow-through while the body stands, leans into turns, starts and stops, the chest, neck and head
  turned toward what it fights (LookAtModifier3D), the feet's IK eased on and off. The rules are unchanged.
- The Ember King's cape is cloth (Cloth3D on his chest bone) that swings and settles.
- tests/emberthrone-motion.mjs: the motion measured from the bones on romdev (the game's motion recorder) through a
  pad script (run, stop, turn, a lock-on fight with sidesteps, a combo, a roll, a heavy, a back step, a back walk) and
  the bot at the Ember King's throne: the joints of a cut break one after another (sync 0.36, peaks spread 2.8 ticks),
  no pop at a clip change (13 degrees at most), the cape swings and settles, the trunk leans into a turn (0.78
  correlation), the stand has no sine-loop joint. The control is the procedural build, which fails every check (sync
  0.92, peaks on one tick, pops of 27 degrees, a still cape, no lean, 8 of 12 stand joints pure sines).
- Checks on this change: Ember Throne's rules (and their 13 controls), soak (won at 19.7 minutes, its control
  failing), flash, grounding, its gates, and its perf gate on the 890M (GL and WebGPU) and on another GPU. The look check
  passes in seven regions; the keep's complementary accent reads 3.7% against its 4% bar (4.4% before): in two of its
  five frames a moving figure no longer stands where it did, the palette is unchanged.
- **beatsync**: the worlds are mirrored in the glass roads. The night sea, the synthwave grid, the rock stage and the
  metal hall light the field (cartwheel-engine's backdrop `environment`): each world is captured from its camera and
  the dark glass under the rivers reflects it, its share following the fade. The road stands for a floor going away,
  so its normals lean back like a floor's and it mirrors the world beyond it (the sea's horizon and harbour lights,
  the grid's sun, the stage's lamps), not what is behind the camera; in those four styles the screen-space reflections
  take the backdrop's picture too (`ssr_backdrop`), faded with the style. The stage's spots and the metal hall's sweeps
  have lamps with glowing lenses, so the lights show in the reflections. A capture waits while two worlds fade (its
  faces and its filter were a millisecond more a fade frame and a spike of three on a Radeon 890M), and the world coming
  in lights the field once its fade is over (a world shown with its environment on is captured on its first frame);
  captures are 64 texels a face over twelve frames. Other styles keep the studio of soft boxes the field reflected.
- **beatsync**: the one-bit style's HUD and prompts are in its two colours, ink and paper (cartwheel-engine's one-bit
  edges and two-colour prompts), fading with the style: the colours move to the ink as the print comes in and the
  one-bit edges come on at half way. A judgement is told apart by its word and its shape: Perfect in an ink frame,
  Good plain, Miss struck through. tests/beatsync-styles.mjs holds that style's HUD to its two colours (every pixel of
  its boxes ink or paper over eight frames of play, ink on paper 17.9:1, the three judgements apart by shape) with a
  control that must fail (the coloured HUD over the print).
- **beatsync**: the metal hall is a lit showpiece: paneled steel walls down both sides with slim pilasters of cool
  light and a gold rail, a ceiling of rows of light strips receding into the distance, a tall fluted far wall with
  slots of light, warm strips along the floor at the columns' feet, the sweeping lamps in view; the deep space nebula
  has form (its billows lit from one side and shadowed on the other, dark lanes of dust, bright threads along its
  folds); the night forest's fireflies are larger, each in a soft glow of its own; the synthwave grid has the sun's
  glow on its sky, long wisps of cloud lit pink from below, streaks of light running in along its lines, and its sun
  in the roads; the rock band has hands (a palm, fingers of three joints and a thumb, each curled to its grip: fists
  round the sticks and the mic, fingers on the neck, a pick at the strings).
- **beatsync**: the sea's water is made in code (cartwheel-engine now draws and reports a node a world gets while the
  game runs), the scene-file workaround gone. The scheduler's style costs are measured again with the reflections
  and its fade budget is 7.2 ms (the club lasers into the rock stage, allowed before, drew 8.1 to 8.5 ms a fade frame);
  the hitch check's tour makes only changes the scheduler could make and holds every fade frame to 8 ms of GPU (in
  Radeon 890M units: a remote lane's GPU times through its fit). A fade into or out of a two-colour print (one bit,
  amber, newsprint) takes twice as long, and the streak milestone swells in over a quarter second; from the results
  straight into a song the field and the HUD come up over half a second (they came up in one frame). On the release
  carts, GL and WebGPU: every style change's frames under 16.7 ms of CPU and GPU (worst CPU 8.0 / 7.6 ms, worst GPU
  5.8 / 8.5 ms), the GL fades under 8 ms (worst 5.8); the control with no prewarm goes over (126 / 256 ms).
- **beatsync**: the engine now draws every world and look once under the loading screen (cartwheel-engine 0.70), and
  the game asks again for the worlds it builds in code and its looks; the game's own prewarm stays for what that does
  not cover yet (the night sea's first showing on WebGPU, a world's first environment capture). The hitch check's
  control turns both off (`env CW_TEST_NO_PREWARM=1`, which the game reads too).
- **beatsync**: known engine issues on cartwheel-engine 0.70.1, an engine fix is coming: half blocks on a palette
  under TAA can draw a flat colour as a checkerboard of two entries (the arcade maze's readability is held out of its
  gate until then: 7.9:1 on 0.67.0); a fade between the arcade maze and one bit costs more than its two looks and spikes as it
  ends (the scheduler leaves that pair out); a fade out of an engine look can cost more than its two pictures (on GL,
  half blocks from the lava into the metal hall take 8.2 to 9.2 ms a frame on a Radeon 890M), and on WebGPU a frame
  where an engine look starts or ends its blend can take up to about 9 ms of GPU there (the hitch check's 8 ms fade
  bound is held; every frame stays under 16.7 ms).
- Every game on cartwheel-engine 0.70.1 (prewarm of backdrops and looks, fog over transparent surfaces, one-bit 2D
  edges, particle start angles): the program tables of beatsync, Destructo, Detexto and skate take the new states
  for transparent surfaces in fog.

## 0.49.0

- **Detexto**'s whole picture is two colours under its 1-bit looks. The engine's menus (loading, title, pause,
  Settings, the Graphics page) draw in the look's ink and paper (`cw_shell_desc.two_colour`): no see-through dimming,
  each line of text on a paper plate over the picture, one-bit text edges, the selected row an ink plate. The HUD, the
  result card, the structure select and the pause screen's controls card draw with one-bit edges
  (`cw_r2d_set_one_bit`, outlines in the paper) and their button prompts in the same two colours
  (`cw_prompt_set_two_colour`); plates and frames are square on whole pixels and a colour under half alpha is left
  out. Every pixel of those screens is the look's ink or paper, at 100% and 200% text, on GL and WebGPU. Changing the
  Look in Settings changes the menus on the next frame; ASCII keeps the engine's menus with the selected row on a
  white plate. Dither warm's ink is 255 229 189 on 9 8 6, the bytes its picture already showed.
- **Detexto** at 200% text: the HUD, the result card, the structure select and the pause screen's controls card lay
  their lines out by their measured size, so nothing overlaps and the plates and boxes grow round the text; the
  structure select shows as many rows as fit and wraps long names; the boxes behind the engine's menus grow with the
  text.
- tests/detexto-dither.mjs: the menus check covers the title, the HUD (aiming, and over the result card), pause,
  Settings and the Graphics page in every 1-bit look at 100% and 200% text: every pixel exactly ink or paper, each
  line's ink within 8% of the blended text's coverage, with controls that must fail (the menus and HUD in their own
  colours and edges, `MENUS_TWO_OFF 1`; one-bit edges cut at 0.95 coverage); a Look change in Settings shows in the
  new colours on the next frame, and ASCII turns the mode off.
- Every game on cartwheel-engine 0.67.0 (from 0.64.1). The program tables were current. Checks on this pin: every game
  builds; the program survey of all nine dual carts on GL and WebGPU hosts at every preset (no state missing; the GL
  host's no-seed player run of Destructo and Detexto never leaves the loading screen in 900 frames, on 0.64.1 as well);
  the cart smoke, version, art and prompt checks of all nine games; every other game's frames in play and in its pause
  menu byte for byte the same as on 0.64.1, GL and WebGPU (the engine's new two-colour and one-bit drawing is off unless
  a game asks); skate's gates and look check and beatsync's style survey (the games that hide nodes); Detexto's look
  gate and its flash check, GL and WebGPU, each with its strobe control failing.

## 0.48.0

- **beatsync**: nothing in a backdrop reads as a note. The arcade maze's bright critters and the pellets down its rivers
  are gone; faint trails of light run its halls instead, a cell a beat, turning at the junctions. Lit windows are tall
  narrow panes, street lamps wide low glows, the forest's and the desert's moons crescents, the fire's ground glow and
  the lava's embers dimmer. The readability check finds each style's most note-like shape with the field hidden and
  holds it under half the notes' own contrast (its control, the old maze, must fail). A new style, **Metal hall**:
  brushed steel plates, chrome columns ringed in gold, gunmetal beams, all reflecting the hall's light, two lights
  crossing it slowly in time with the music.
- **beatsync**: every river's note is as big a target. The bass note is a triangle (it was a flat bar across its
  river, about four fifths of the others' area) and the synth's a square, each of the same area as the round kick and
  the clap's diamond; their gems a triangular prism and a square slab. tests/beatsync-footprint.mjs counts each river's
  note pixels on black from the play camera (its control, the old bar, must come out smaller).
- **beatsync**: no stutter when the style changes. A world or a look drawn for the first time made its programs,
  pipelines, particle buffers and targets on that frame (20 to 350 ms the first time each style showed), and the fire
  restarted its particles every time it came back (85 to 350 ms each time). Now every world and every kind of fade is
  drawn once behind the loading screen before a song (now over black: the changing worlds showed round its panel),
  and a world that leaves comes back where it left off.
  tests/beatsync-hitch.mjs times every frame of two rounds of all the styles fading in and holds each to a 60 Hz frame
  of CPU and of GPU, on GL and WebGPU (its control, a cart with no prewarm, must go over).
- **beatsync**: twenty-one styles. Eight new here (and Metal hall above): **Aurora** (curtains of aurora folding
  and rippling slowly over a snow field, dark pines, a far range under snow), **Deep space** (a nebula of glowing
  gas, three depths of stars, a ringed planet lit from the side with a thin atmosphere, dust streaming past),
  **Rain city** (towers lit window by window, neon blade signs, rain, the wet street a mirror of it all with rain rings on it), **Night forest** (moonlit pines,
  shafts of moonlight, low mist, fireflies glowing up and down), **Underwater** (caustics dancing on the sand, shafts
  from the rippling surface, kelp swaying, a school of fish, bubbles, the water thickening with distance), **Desert
  night** (dunes under a low moon in amber dither), **Newsprint** (the rain city in ink on paper) and **Lava** (rivers
  of lava creeping toward the camera and a volcano pouring lava, in half blocks). The older styles reworked:
  **Fire** is walls of real flame (curtains of tall licking tongues with white-hot feet, from a flame flipbook baked
  by `tools/bake-flames.mjs`, under a sky of smoke that now draws: a map that repeated one way only was skipped); the
  **Rock stage** frames the band closer, rims each player with the back light and moves them more (hips swaying, the
  weight shifting, the nod deeper in loud stretches, the singer's free arm out to the crowd); the crowds at the club
  and the stage dance, hands going up as the music gets louder, and are rimmed against the haze; the **Synthwave
  grid**'s sun sits half set and its roads are near opaque (notes 6.6:1, from 4.8:1); the **Night sea**'s near swells
  are navy; **Clean**'s valley opens onto a massif with two great snow-shouldered horns. The scheduler never fades
  between two worlds whose costs add up to more than 8.5 ms on a Radeon 890M (the worst fade measured at High: 7.1 ms
  GL, 6.2 ms WebGPU). tests/beatsync-styles.mjs holds the notes to 4:1 (from 3:1) in every style, each with its own
  readability control, and each style's fade in and out to the flash rules with its one-frame-cut control. The program
  table covers every world's draws at every preset (the programs gate also reads the cart log's NOT DRAWN states: a
  world's states reached the engine's list only from 0.65.1).

## 0.47.0

- **beatsync**: the styles rebuilt as a showcase. Every backdrop is now a world of its own behind the field
  (cartwheel-engine's Backdrop nodes, `backdrops/*.cwscene`): Fire is a canyon of fire walls receding to a burning
  horizon over cracked, glowing ground, under smoke lit from below, with an ember storm and heat shimmer; Club lasers
  is a club in haze with laser fans sweeping over the crowd and three moving heads throwing volumetric beams; Clean is
  a lit landscape where the other track rises as hills beside a valley with a still river under a low sun and a
  procedural sky; the Synthwave grid gains stars and wire mountains, and its land is a dark skyline with lit tops.
  Four new styles: **Rock stage** (a band of four skinned players, guitar, vocals, bass and drums, playing loops on
  the beat with their hands placed on their instruments, under moving spots in haze, an LED wall and the crowd in
  front), **Half blocks** (the engine's half-block look over the grid, dissolved in and out) and **Arcade maze** (an
  original style after the early arcade maze games: a maze of double neon walls riding toward the camera with star
  critters in its halls, the rivers as corridors with a line of pellets down each, in chunky pixels on a small
  palette) and **One bit** (the rock stage in the engine's ordered dither at its display stage: every pixel warm white
  or near black in a Bayer cross-hatch, outlines from the depth). The **Night sea** is back in the rotation: the
  engine's water lit by the moon with no procedural sky, its path glittering down the swell. Every style but the
  characters draws the field lit: notes as gems (an orb, a diamond, a capsule, a prism) that glow their colour and
  catch the field's lights, a soft halo under each, glowing banks and gates, roads of dark clear-coated glass; the
  field's lights take each backdrop's colours and sweep slowly. A fade between two worlds keeps the one going out
  under the one coming in (a straight cross-fade, never more than two drawn). The multiplier and the streak callout
  sit under the middle judgement with a clear gap: they no longer overlap it. Debug requests `style pin`, `hud old`,
  `bdcam`, `bdset`. tests/beatsync-styles.mjs adds the HUD check (no two lines of HUD text drawn over each other;
  control `hud old 1`) and plays each style on a fresh start of the song. A world out of sight is hidden as well as at opacity 0, so it does not tick (its particles pause). Every game on
  cartwheel-engine 0.64.1. Near
  its gate a gem gives way to the note's flat shape. The flash fixes found on the way: the half blocks draw each
  square's mean colour (stepped colours jumped as a light passed), the laser fans sweep over eight bars and stop short
  of the camera, the arcade maze's pellets stand still and its walls ride a cell every two beats, the grid and the
  half blocks leave the land out between two players' roads. The duo gate's covered-river control draws its cover in
  front of the lit notes; the streak's drain is a deeper red.

## 0.46.0

- **Detexto** draws its 1-bit looks with the engine's one-bit dither (`cartwheel/look.h`) at the display stage: last,
  on the finished picture, one dither pixel a screen pixel, with the game's own tone (the landmark's hues by their
  value, the grey world a little darker with its highlights kept, a light unsharp mask, depth outlines, gamma and
  contrast) and the desert levels' lift of the landmark against its sand. The game's own dither pass and its shaders
  are gone. Every pixel of the 3D picture is still ink or paper on GL and WebGPU and nothing crawls with the camera
  still. The picture keeps 0.44.0's tone (within 0.8% of full scale over 16x16 pixel blocks, 1.3% on the dam); the
  dots themselves are the engine's blue noise and Bayer matrices, so about a fifth of single pixels differ (a third in
  Dither grid). Dust, smoke and the plume stay at half opacity under these looks. The pass costs about 0.57 ms on
  WebGPU on a Radeon 890M at 1080p (Dither grid 0.64; the old pass 0.40). ASCII stays the game's own pass: it draws
  its characters at the output's size from a scene rendered at 35%, and the engine's looks draw at the scene's size.
- **Detexto**'s menus (title, pause, Settings, the Graphics page) show the selected row as a white plate with black
  text instead of the yellow accent, and the Graphics page's backing box is wide enough for its rows. The rest of the
  shell's menus still dim the picture behind them and draw their other rows in white at 85%, so a menu screen is about
  90 to 93% ink and paper (as in 0.44.0).
- Detexto gates: tests/detexto-dither.mjs also checks the menus (the selection style on every screen and their share
  of ink and paper), and its crawl control is the engine's `CW_TEST_LOOK_CRAWL`, sent with the load. New
  tests/detexto-look-compare.mjs: the same scenes against another build, tone and single pixels, with a flat-tone
  control that must fail.
- Every game on cartwheel-engine 0.64.0 (from 0.60.0). The program tables were regenerated and came out unchanged.
  Checks on this pin: every game builds; the program survey of the dual carts of beatsync, destructo, detexto,
  lanternfall and pinball on GL and WebGPU hosts (no state missing; the native survey was not rerun); the cart
  smoke, version, art and prompt checks of all nine games; Destructo's, Detexto's and Emberthrone's water byte
  for byte the same as on 0.60.0 (each lit by its procedural sky, which the engine's new water lighting leaves
  alone); the flash check of real play in each Detexto look, GL and WebGPU, each with its strobe control failing.

## 0.45.0

- **beatsync**: firmer haptics. A miss is a 120 ms bump at about half strength (was a faint 90 ms one), a broken
  streak a little stronger, and losing the crowd a long buzz that tapers over about 1.35 s on every playing pad (was
  300 ms on the first pad only). New gate: tests/beatsync-haptics.mjs (no rumble in the count-in, a bump a miss, the
  loss's three steps).
- **beatsync**: the tempo on the shoulders goes from 50% to 150% (was 70% to 130%). The tempo gate also checks both
  ends: every note Perfect at 50% and at 150%.

## 0.44.0

- **Detexto** is drawn in one bit by default: every pixel of the 3D picture is black or white at the screen's full
  resolution, a blue noise dither anchored to the screen (a still camera gives a still picture), after a tone step that
  reads the landmark's hues by their value, sharpens edges lightly and outlines every silhouette from the depth. The
  HUD and the game's own screens are in the same two colours; the structure select draws solid tiles with the choice
  inverted. A new Settings row, **Look** (saved), picks **Dither**, **Dither warm** (warm white on near black),
  **Dither grid** (an ordered cross-hatch) or **ASCII**, the neon characters as before. The desert levels lift the
  landmark against its sand, so one lit from behind no longer reads darker than the ground; dust, smoke and the
  collapse plume draw at half opacity under the 1-bit looks, so a structure reads through them. The pass costs about
  0.3 to 0.5 ms a frame on a Radeon 890M at 1080p (GL and WebGPU); the scene renders at full size under the 1-bit
  looks (ASCII keeps 35%). New gate: tests/detexto-dither.mjs (two colours, the patterns, no crawl with a still
  camera, the Settings row; each with a control that must fail), on GL and WebGPU. The launcher art and the README's
  picture are taken in the new look. The cart smoke check counts a dithered two-colour picture as a picture (two
  colours, each a real share of the frame); a blank or single-colour frame still fails.

## 0.43.0

- **beatsync**: visual styles that change with the music. A style is a look (the ASCII pass on or off), a backdrop, a
  palette and the field's params; the song changes style at phrase boundaries (every 8 bars) and at drops, faded over
  2 beats (a drop's over 1, never under half a second), each song starting in the neon characters and keeping its own
  order. Six in the rotation: Neon characters, Clean (the same land, plain, its tops lit), Fire (the engine's baked fire
  flipbook, embers, heat haze), Synthwave grid (a grid floor to the horizon, a striped sun, the land as see-through
  mountains), Terminal green, Club lasers (three fans sweeping slowly through haze, spot lights in a fog volume). A
  Night sea style on the engine's water is built and held out of the rotation until the water draws at night without a
  procedural sky. The rivers, notes and gates never fade; every coming note rides in a dark pocket, and a style with a
  backdrop lays a dark road under each river. **Visual style** in Settings: all styles or one. Debug request
  `style <name|index>|auto|next|cut|readctl|tour`.
- **beatsync**: no regular flicker on a run of eighths: a run on one river draws at 60%, and each river keeps its own
  judgement word under its gate instead of one word hopping from gate to gate on every hit (Test Groove on Hard read
  0.029 against the 0.02 bound).
- New gate: tests/beatsync-styles.mjs (every style shown, a still and motion frames saved for review; the notes at least
  3:1 against their river in every style; romdev's flash check through every fade; controls: no roads or pockets with
  dim notes, every change a one-frame cut).
- Every game on cartwheel-engine 0.60.0 (Settings rows of the game's own).

## 0.42.0

- **beatsync**: the shoulders change the tempo while a song plays: L slower, R faster, 5% a press from 70% to 130%,
  on either pad, with the pitch kept (cartwheel-engine 0.59.1's time stretch). The HUD shows the BPM as played and
  the percent; the count-in shows the two buttons. The notes, the clock and the judgement follow the tempo (the windows
  stay the same in real time) and a hit scores times the tempo. The two-lane game plays on West and South only. New
  gate: tests/beatsync-tempo.mjs (in time at 120%, the kick heard where the clock says, the pitch kept).
- Every game on cartwheel-engine 0.59.1.

## 0.41.0

- **beatsync**: long song names fit their boxes everywhere. In the song list a long title gets a little smaller and then
  ends in an ellipsis, clear of its BPM; the selected song's panel shows the whole title on up to two lines (the lines
  under it move down); the difficulty screen's heading, the box during play and the results do the same as the list.
  Never smaller than the engine's text floor.
- beatsync's song tools: `tools/measure-sustains.mjs` measures how long the bass and the other track hold their
  notes in packages already made (how many ring a beat or more, a song and a minute), the question before held notes
  are charted.

## 0.40.0

- Engine v0.58.0 (from v0.57.7): a game can add its own items to the shell's pause menu; leaving the pause menu starts
  play on the next tick (a press no longer carries into play). The program tables stand as they were.
- **beatsync**: Start during a song offers **Choose another song**, straight back to the song select (on the same song),
  without quitting to the title.

## 0.39.0

- **beatsync**: one player's two lanes sit closer together (3.2 either side of the middle, from 4.2): their busy
  notes are drawn dimmer since 0.36.0, so the closer rivers stay under the flicker bound on the densest groove (0.013
  against 0.02; at 2.8 it was 0.028).

## 0.38.0

- **beatsync** finds its songs in its own assets when it starts: every `songs/<id>/song.json` the host lists
  (wasmcart `_filelist.txt`), in `songs/order.txt`'s order then by artist and title. A cart unpacked to a folder gets
  a song by having its folder dropped into `assets/songs/`, no rebuild. Up to 512 songs (bests are kept for the first
  32 played: the shell caps a game's save at 6 KB).
- A song may carry its vocals (`vocals.ogg`, `tools/make-song.mjs --vocals`): the game plays them as recorded, on their
  own bus, never reacting to the player. The songs that ship are instrumental.
- beatsync starts the mixer itself with 12 streams (a song's five tracks while the last one fades out) and updates it
  each frame: the shell's 8 ran out on a change of song with five tracks, and the new song started silent.

## 0.37.0

- Engine v0.57.7 (from v0.57.2): the join screen centres its seats for any number of players, program tables stamped
  with the extension sources they were made from (the build stops on a stale one), values a program lacks reported by
  the program survey, the clustered lights' shadows drawn from each view's own casters in a split screen drawn in one
  pass. Every program table is regenerated and stamped on the new engine.
- **beatsync**'s join screen draws its box behind each seat where the engine now puts the seats
  (`cw_shell_join_seat_rect`), centred under the heading; it had copied the old layout, which sat both seats on the
  left half. **Ember Throne** and **Lanternfall** (two players) get the centred seats from the engine.
- Checks run on this pin: every game builds (native and dual carts); cart smoke of all nine games, cart art (its
  no-cover control caught), cart version (its rewritten-version control caught) and the prompt check of every game;
  the program survey of every app, native (every preset and the switches between them, GL and WebGPU): no state
  missing, and of every dual cart on a GL and a WebGPU host: no state missing and no value set for a program that
  lacks it. The check cart `destructo_check` draws no 3D frame and has nothing to survey; Destructo's and Detexto's
  unseeded player runs were still on their loading screens at the survey's frame count (as v0.36.0's carts are) and
  were read at their title screens instead, nothing missing. Skate's one-pass gate (`tests/skate-onepass.mjs`, two and
  four players, GL and WebGPU) against v0.36.0's build: 0.0000% of the pixels off at frames 600 and 1400, both
  controls failing (vignette 46.9 to 48.0%, masks 2.7 to 3.3%). beatsync's gates (`beatsync-song`, `beatsync-menus`,
  GL and WebGPU, controls failing) and the two-player gate (`tests/beatsync-duo.mjs`, GL and WebGPU, all five
  controls failing). The water of Destructo and Detexto (the harbour level) and Ember Throne (the marsh) against
  v0.36.0's carts on GL and WebGPU: the same to the pixel, against a control 6 frames later that differs in 10 to 63%
  of the pixels. The join screens of beatsync (no seat joined, both joined), Ember Throne and Lanternfall looked at on
  romdev at native scale on GL and WebGPU: the seats centred under the heading, beatsync's boxes behind them.

## 0.36.0

- **beatsync** plays with one or two players. One player chooses three lanes (kick, clap, bass, as before) or two
  lanes: the **drums** (kick and clap, the hats on the clap) or the **groove** (bass and the **synth**, the record's
  chords, stabs and lines, now playable). Two players each play a pair on their own pad, each on their own road (two
  highways side by side, two vanishing points, the land between them),
  each at their own difficulty, each with their own streak and multiplier (a miss breaks only your own, on your
  road), one score and one crowd; left or right swaps the pairs, both press
  ready. Play opens the join screen (Start plays alone), and a second pad can also join with one press on the song
  select or the difficulties.
- Two lanes play on West or the left shoulder and South or the right shoulder, both always live; East stays the
  three-lane bass. A touch device gets the host's gamepad overlay, which now shows the shoulders too.
- The two-lane charts carry more notes a button than the three-lane game at the same level (Easy on the eighths,
  Medium eighths on both hands, Hard sixteenths), and never fewer than the three-lane chart gives those parts. All six
  songs are re-charted with them (song.json `pairs`); their three-lane charts are unchanged byte for byte.
- Each player's notes are judged in their own difficulty's windows, a press counts only for its own player's lanes,
  and the pad that missed rumbles. The results show each player's part and accuracy. One player's bests are kept for
  the three lanes and for each pair (an older save keeps its bests and calibration).
- The rivers glide to the layout chosen on the difficulty screen; the synth has its own colour in every palette and
  hexagon notes. One player's two rivers sit further apart than the three-lane game's: two rivers of dense notes in
  one sixth of the screen were a regular flicker (0.027 against the 0.02 bound on the densest groove; now under
  0.005). With two players the streak badge sits between the two roads, clear of the judgements. On two lanes the busiest rivers' notes are drawn dimmer (the clap, which carries the hats on the
  eighths, at half light; the groove's bass on its Hard sixteenths and the synth less): the densest drums flickered at
  0.043 and the densest groove at 0.036, both now under the bound. With two players each has their own Perfect,
  Good or Miss under their own road (one word jumping between the roads on every hit blinked at 0.039). A part nobody plays sits at full in the mix; the synth played makes
  the other track a lane.
- New gate `tests/beatsync-duo.mjs` (GL and WebGPU): the autopilot plays Test Groove as two players at two
  difficulties (and swapped) and as one player on each pair, every note hit, each player's notes exactly their
  pair's chart; two pads reach two-player play through the menus; pad 2 alone hits player 2's notes and none of
  player 1's; every river each layout shows is lit over its gate in the picture. Controls: the counts held to the
  difficulties swapped, only one player ready, each player on the other's pad (`padswap 1`), a river not shown drawn
  dark where it rests (`rivers hidden draw`, which covered the three-lane clap river in a first build); player 2's
  miss breaks player 2's streak and not player 1's (control: one shared streak, `sharedstreak 1`). The flash check adds the drums and the groove on two lanes and two players on four rivers,
  played whole on Hard. `check-songs` holds the two-lane charts (control: each pair cut to just under its rules,
  caught on every pair and level), and the chart unit tests cover them. The menu inputs of the gates go through the
  join screen.

## 0.35.0

- **beatsync** draws only what tells the player something. The ring on every kick of the record, the sparks on every
  clap, the bass waveform along the bottom and the field of glyphs following the loudness are gone: each repeated
  what the player already heard.
- **beatsync** shows the music coming: beside the rivers, out to the screen's edges, a landscape of characters that
  is the other track (the chords, pads, stabs and synth lines the player does not play). It is a row every half beat
  riding down with the notes, so a loud stretch rises on the horizon before it is heard and a break flattens the land
  ahead. A row rises with the track's loudness and its bass, mid and treble laid outward; it takes the colour of the
  harmony sounding then, so a chord or key change is a new colour travelling toward the gates; stabs light its ridge
  and a chord change runs a crest along it. A faint sky above the horizon holds the harmony sounding now.
- The song tooling analyses the other stem once per song (`tools/lib/look.mjs`, song.json `look`): loudness, tone,
  harmony on the circle of fifths and how clearly it sounds, the beats where it changes, bass/mid/treble per half beat
  against their own average, and the stabs. `make-song.mjs --look-only` adds it to a package from its source without
  touching the charts or tracks; all six songs have it. `check-songs` makes the look again from the decoded other
  track and holds every beat's loudness to it (control: the look moved by two beats fails). Unit tests against known
  chords and the synthetic song's one chord a bar, with controls.
- **beatsync**'s streak is where the eyes are: the multiplier (x1 to x4) and a bar of the hits to its next step sit
  under the middle gate. The streak charges the banks with light from the gates up, to the horizon at x4; each step
  sends a band of light up the rivers; every 50 is called out. A streak broken after a multiplier drains back down
  the banks in red, its lit pieces fall away, the pad rumbles, and "streak N lost" shows what went.
- New gates through romdev on GL and WebGPU: `tests/beatsync-land.mjs` (every row drawn is the song's analysis, the
  land changes with the music and the picture follows it; controls `land off` and `land flat` fail) and
  `tests/beatsync-streak.mjs` (the same deterministic run with and without the streak's picture: the charge shows and
  reaches the far end at a streak of 50, the break shows in red; control: the run without it against itself). The
  flash check meters the multiplier band and the drain, with a break in its run.
- The land passes the photosensitivity check over whole songs. A row every half beat passing one point of the screen
  was a regular flicker near 4 Hz (amplitude 0.10 on the densest song, the bound is 0.02). Now each row reaches down
  behind the nearer one (no dark gaps), keeps its roughness by column, does not fade below the gate and spreads its
  bands over a beat either side; its stab lights are soft. The land and the banks' charge sit under the rivers' light,
  and the streak no longer brightens the moving current. The bass bar, the most light a run of eighths passes the gate,
  is drawn a fifth dimmer than the round notes (0.023 down to 0.015 at its gate on the densest song).

## 0.34.0

- Engine v0.57.2 (from v0.53.4): what each split screen view draws said by masks, a split screen drawn in one pass
  with more of the passes (probes, SSR, SSGI, the depth prepass, distortion, the viewmodel, planar water), each view
  sent to the GPU as it is drawn on WebGPU and GL, a lens per view on a shared post chain, randomized sounds and
  interactive music, a sound's own loop points. The program tables are regenerated on the new engine (Destructo's,
  Detexto's and Ember Throne's water reads its view's rectangle).
- **Skate** draws its split screen in one pass: each player's sky dome sits round that player's camera and the tokens a
  player has taken are gone from that player's view only, said once a frame as masks of views instead of a hook run
  before each view, and the post chain is shared with its vignette kept in each view. At two and four players, GL and
  WebGPU, every preset (Very Low to Ultra), the frame runs in one pass. The picture is the old one: new gate
  `tests/skate-onepass.mjs` holds it to v0.33.0's build at frames 600 and 1400 (bots skating Sunnyside) within 0.3% of
  the pixels off the seams (measured 0.0000% to 0.0046%), with two controls that fail: one vignette over the whole
  screen (47 to 48%) and each view given the next view's dome and tokens (2.7 to 3.3%).
- Wall time a frame, Skate's release cart on a host that waits for each frame (romdev), v0.33.0's view hook code against one pass on
  the same engine, interleaved rounds, on a Radeon Navi 33 (the load printed was 4 to 5 from other work, so the
  medians carry noise):

  | backend | players | view hook ms | one pass ms | CPU ms | GPU ms | GPU clock |
  |---|---|---|---|---|---|---|
  | GL | 2 | 6.24 | 6.08 | 3.60 to 3.45 | 1.12 to 1.28 | 2260 / 2155 MHz |
  | GL | 4 | 9.61 | 7.71 | 6.09 to 5.00 | 1.86 to 1.95 | 2172 / 2034 MHz |
  | WebGPU | 2 | 6.41 | 6.63 | 4.26 to 4.06 | 1.40 to 1.47 | 2211 / 2278 MHz |
  | WebGPU | 4 | 10.62 | 10.63 | 7.06 to 6.89 | 1.76 to 1.66 | 1608 / 1579 MHz |

- Checks run on this pin: every game builds; the program survey of every app (native and dual carts, GL and WebGPU,
  every preset and the switches between them): no state missing; cart smoke of all nine games, cart art (its no-cover
  control caught), cart version (its rewritten-version control caught) and the prompt check of every game; Skate's
  gates (skate-auto, skate-play) on GL and WebGPU and the new one-pass gate; Destructo's play and newcomer gates on
  the new engine; the sea of Destructo and Detexto (a harbour level) and an Ember Throne run against v0.33.0's carts
  on GL and WebGPU: the same to the pixel in the water; Skate looked at on romdev at native scale: two players with pad
  play (each view's sky round its own camera as it turns, the vignette in each view) and four bots (a token taken by
  one player gone from that player's view and still in another's, against a control that swaps the views' tokens). No game uses the sampler loop points the new audio reads (none ships a
  WAV) or motion blur.

## 0.33.0

- **Towers** gives stars: a map held earns one, two with the core at 10 or more, three when no creep reached it. Three
  stars sit on the top line beside the core's count, and each one dims over half a second as the core falls past
  what it asks, so a player sees what a leak cost while it happens. The win card grows the stars won in one after
  another and says what the next one asks ("the core at 14 of 20: let no creep reach it for the third star"); the map
  screen shows the map's best, the total ("STARS 12 OF 90") and the three rules. Each map keeps its best in the save,
  after the two fields the save already had, so a save from before keeps its maps and starts with no stars (the
  shell keeps the bytes both sizes hold since engine v0.51.0). New gate `tests/towers-stars.mjs`: an old save keeps
  best map 7 and 9 maps won with no stars (control: another save version with no migrate hook starts over and
  fails), and through romdev on GL and WebGPU map 1 held from a core of 20, 14 and 6 gives 3, 2 and 1 stars and saves
  them (control: stars shown but never written, saved 0, fails). The HUD overlap gate passes with the wider top line
  (every case and sweep on GL, its 20 px control failing) and the flash sweep's towers-boss stretch passes on GL and
  WebGPU.
- **Pinball**'s chrome balls, post lamps and flashers throw small four-ray stars where a lamp's highlight burns
  (the engine's glints, threshold 1.5, short rays; off on Low and Very Low). The flash sweep's storm stretch (three
  balls, every shot pulsing) passes on GL and WebGPU, with its 8 Hz strobe control failing.
- Tried and not kept: motion blur on Skate and Destructo. The blur follows the camera's motion, and in both games the
  camera moves with what the player watches: in Skate the park smears in every turn while the skater, board and
  wheels stay sharp, and in Destructo the field softens in every camera glide while the flying blocks hardly move
  between two frames. Neither looked better. Jewels keeps its own star glints: the engine's glints at any threshold
  either flood the board with soft crosses on whole highlights or leave it bare, where Jewels' pass puts one small
  star on the brightest facet of a patch.

## 0.32.4

- Engine v0.53.4 (from v0.50.3: reflection probes and probe grids captured with their own camera's light under
  clustered lights and cascaded sun shadows, a probe captured once no longer dark on WebGPU, materials take up to 8
  shading parts, a shell save that grows at its end keeps its old bytes, TAA on moving things, the first GL frame
  drawn again). **Pinball**'s chrome ball now mirrors the lit table: its reflection probe (clustered lamps, captured
  every third frame) was lit with the main camera's light data and showed a dark disc; on romdev the ball in the
  plunger lane averages 115.1 levels where it was 103.6 (a disc 30 pixels across), GL and WebGPU within 0.01 of
  each other, its lower half now showing the lit table where it was dark.
- **Towers** on Very Low asks the new engine for one more draw state, its rim-lit instanced bodies under the plain
  shading (`standard+vcol+instbig+fog2+icol+l2001+xcluster_plain_rim`); the table has it now (Tint accepts all 22
  WebGPU programs), where without it those bodies would not be drawn. Through romdev the auto cart on Very Low reports
  no missing state on GL or WebGPU; the old table reports that one.
- Checks run on this pin: every game builds; the program survey of every app of every game (27 apps, GL and WebGPU,
  every preset and the switches between them): towers gained the one state, every other table unchanged, and Skate's
  and Towers' six apps surveyed again on v0.53.4 with no missing state; cart smoke of all nine games, cart art (its
  no-cover control caught), cart version (its rewritten-version control caught) and the prompt check of every game;
  towers' gates (towers-match, towers-build) on GL and WebGPU; pinball's probe-lit ball on romdev against v0.32.3's
  cart on GL and WebGPU at native scale; the skinned characters of Skate and Ember Throne and Towers on Very Low
  looked at on GL and WebGPU; Jewels, the one game with TAA, with its stones rocking and swapping: the board the same
  as v0.32.3's on WebGPU pixel for pixel and on GL within one level on 9 pixels (its stones are traced in a pass and
  carry no motion of their own, so the new TAA terms never apply).

## 0.32.3

- **beatsync** hits feel like hits: every hit bursts at its gate in the river's colour, shards thrown up and out and a
  ring opening out, bigger, faster and whiter on a Perfect. It swells in and fades over half a second (the flash
  checks pass on the densest songs played whole).
- **beatsync** shows each judgement in one colour on every river and on the results: Perfect green, Good turquoise,
  Miss red (they took the river's colour before).
- **beatsync**'s bass is heard: hitting or missing a bass-lane note changed nothing audible. A bass-lane note made from
  the other track in a break (20 to 70% of them) acted on the silent bass track; it now acts on the other track, which
  sits lower between hits in those stretches, so a stab rings on a hit and is cut short on a miss (7 dB in the mids).
  The real bass, 8 to 11 dB under the drums, now sits lower between hits and a hit lifts it over the record, so a miss
  is 10.5 dB under a hit in the low end (it was 6) and a hit stands 2.7 dB over the record (it was 0). New gate
  `tests/beatsync-bass.mjs` measures the whole mix, with the old levels (`bassmix old`) and the old routing
  (`othersrc 0`) as controls that must fail. song.json marks a note made from the other track with `o: 1`.

## 0.32.2

- **beatsync** Medium plays the bass: every song's Medium chart had almost no notes on the bass river (Test Groove
  1, the others 8 to 11% and only in breaks), because the drums own every beat and the chart picker gave each bar to
  them. Medium now keeps at least one real bass note in every bar where the bass plays, placed where it costs the
  drums least: 25 to 49% of Medium's notes are bass, with the kick and clap still there. Easy stays kick and clap;
  Hard is unchanged. The six songs are re-charted from their sources (tracks byte for byte unchanged), and
  `tools/check-songs.mjs` fails a Medium chart under 15% bass in a song with a bass line (control: the chart with
  its bass taken out).

## 0.32.1

- **beatsync** is forgiving: the judgement windows were 35 ms for Perfect and 70 ms for Good on every difficulty, so a
  player had to hit almost the exact moment, and any delay in the sound path (a browser, a TV, a wireless speaker)
  ate the whole window. They are now 75 and 150 ms on Easy, 55 and 110 on Medium, 35 and 85 on Hard. A wrong button
  no longer misses the note it was meant for: it breaks the combo and that note can only be Good, so a slip is
  forgiven and mashing every button scores Good at best with no multiplier.
- New gate `tests/beatsync-forgiving.mjs` through romdev on GL and WebGPU: a player pressing 50 ms late on average
  with a 50 ms spread and a slipped thumb every 15 notes clears the first song on Easy (accuracy 0.70 or better) and
  Medium (0.55 or better). Controls that must fail: the same player under the old judgement (the crowd leaves on
  Easy) and a masher on every button (no Perfect, no combo).

## 0.32.0

- **beatsync**'s rivers recede in true perspective: each is a road going away toward a horizon above the screen, its
  far end high up and a quarter of its width at the gate, the current, banks and beat lines fading into a haze there.
  A river shows twice the beats it did (Easy 8, Medium 6.4, Hard 5.2), packed toward the horizon, while a note covers
  its last beat within about 10% of the distance it did, so the timing reads as before. Notes take the road's
  perspective: small far off and growing all the way to the gate, with no brighter burst on the beat (their light
  near the gate grows only with their size), and the flash checks pass on the densest songs.
- **beatsync**'s mix answers the player: between hits the kick, clap and bass sit a little under the record with
  their top softened, and every hit punches its part to full and opens it, falling back over a quarter second (on
  Easy the kicks you play stand about 4 dB over the ones you do not). A miss now drops only the part missed, about
  13 dB with a low-pass, and it comes back by itself over a beat or at the next hit, so a few misses never empty the
  mix; the other track and the song's level never move in play.
- **beatsync** never keeps a player waiting: play starts 8 beats before the first note of the level chosen (after the
  count-in), and the five songs are re-charted so no level leaves more than two bars without a note: in breaks
  and intros the charts play what is there (claps and hats on the clap lane, bass onsets, and where the drums and bass
  are silent the other track's chords and onsets on the bass lane), and every level's first note comes within a beat
  of the song's first sound. The song tooling enforces it (`make-song --charts-only` re-charts a package without
  touching its tracks; `check-songs` holds the longest gap and the first note, a chart with a 30 s hole as its
  control). If a wait of more than three bars remains, the banks fill with light as it passes
  and a countdown over its last four bars says when the notes come back.
- New beatsync gates through romdev, each with a control that must fail: `tests/beatsync-depth.mjs` (notes grow from
  the far end to the gate in every lane, twice the beats on screen, the last beat's distance; controls: every note one
  size, notes shrinking as they come), `tests/beatsync-mix.mjs` (from the recorded sound: a kick miss dips only the
  kick and it recovers, the clap, bass and other tracks within 0.5 dB; a hit kick stands 3 dB over an unplayed one;
  controls: a miss that ducks every lane, the reward off), `tests/beatsync-wait.mjs` (every song and level starts
  within 8 beats of its first note; the wait indicator shows in a long wait and is gone once notes are back;
  controls: songs played from the top, the indicator always up). New launcher art and README picture.

## 0.31.0

- **beatsync** plays on a note highway: three rivers of neon characters side by side, laid out as the buttons sit, the
  kick on the left (west), the clap in the middle (south) and the bass on the right (east), so which button a note
  wants is where its river is. Each river is a current streaming down to a gate near the bottom; a note rides it down,
  nearer and larger as it comes, and reaches the gate on its beat, condensing from `+` to `#` to `@`. A hit blooms the
  gate and surges the river, a miss dims the river until the next hit, and a streak brightens the scene. Easy shows
  four beats of each river, Medium 3.2 and Hard 2.6, so Easy reads furthest ahead, and draws its notes brighter; the
  timing windows are unchanged.
  The count-in shows each button under its river, and no button stays on screen in play. The title spreads the rivers
  round its box. A new gate, `tests/beatsync-layout.mjs`, presses each button alone and finds from the picture which
  river lit, on GL and WebGPU: west's left of south's left of east's, with the rivers swapped as the control that must
  fail. The look gate measures the play field between the HUD's columns, and the flash gate's picture check adds the
  densest song played whole on Easy. New launcher art and README picture.

## 0.30.0

- **beatsync**, a new game: a house-music rhythm game on three buttons. You play the kick, the clap and the bass of an
  instrumental house track on the three lower face buttons, and the song plays the way you play it: each part sounds
  at full volume while you hit it, and a miss ducks it with a short fade and low-pass sweep until your next hit. Notes
  are placed and judged on the song's audio clock (Perfect within 35 ms, Good within 70), with a timing calibration
  screen. Every frame is a step sequencer in neon characters (Detexto's ASCII pass): a ring per instrument turns with
  the song so each note reaches 12 o'clock on its beat, kicks send shockwaves of characters out from the centre, claps
  throw sparks and the bass is a waveform along the bottom. Six songs from 121 to 129 BPM, each on Easy, Medium and
  Hard, with scores, grades and bests saved. On a phone the host's on-screen pad shows the three lane buttons, the
  d-pad and Start. The song tooling (Node and WebAssembly, in `games/beatsync/tools`) makes a song from a stems file:
  it splits the drums, finds the tempo, beats and bars, picks onsets per lane, writes the three charts and proves the
  tracks sample-aligned; CI runs its unit tests and checks every song. Gates on GL and WebGPU through romdev, each with
  a control that must fail: sync (an open-loop bot gets every note Perfect on Hard; 50 ms late gets none), newcomer
  (a cue-reading bot with a human's timing error clears the first song on Easy on a pad), look, flash (the two
  densest songs played whole on Hard held to the photosensitive flash rules), programs, text, and the autopilot and menu gates;
  flash sweep entry `beatsync-hard`.

## 0.29.0

- On a phone or tablet the wasmcart host now draws the on-screen pad, from each cart's manifest `controls`, which the
  build works out from the game's bindings: Emberthrone, Lanternfall, Pinball, Skate and Towers list the pad parts
  they read. **Jewels**, **Destructo** and **Detexto** play by touch alone (tap or drag a jewel; drag to aim, tap to
  fire), so they declare `controls = none` and the host draws no pad over the board or the field.
- Engine v0.50.3 (in a cart the host draws every on-screen control and the engine claims no touch; a touch player's
  prompts show pad buttons; manifest controls from the input map; a level loaded in play under the loading screen).
  Every game builds and passes the smoke, art, version and prompt checks on it; through romdev a finger tap starts a
  Jewels game with no engine controls drawn, and Detexto renders on GL.

## 0.28.0

- **Detexto**, a new game: Destructo drawn as neon text. The same landmarks, launcher, ammunition, physics and modes
  for one to four players taking turns, with every frame turned into glowing characters on black. Each cell of a
  54-row grid takes the character whose shape fits the scene there (Fira Mono's glyphs measured in six regions, line
  characters for edges, a brightness ramp for flat areas), held against shimmer, in its material's neon colour
  (stone cyan, steel violet, glass mint, wood amber, red paint hot pink, gold yellow) over a soft glow; the world
  round the field is a dim matrix green. The scene renders at 35% of the output and the characters at its full size,
  crisp: at 1080p on a Radeon 890M (WebGPU) the GPU takes 1.33 ms a frame (4.42 ms with the scene at full size) and
  about 2 ms at most through a collapse. The HUD and menus sit in neon-edged boxes of 70% black, their text glowing
  at full opacity; the result card names the landmark. Gate `tests/detexto-look.mjs` and flash sweep entry
  `detexto-collapse`, on GL and WebGPU.
- Engine v0.48.0 (overlays draw at the output's full size over a scaled scene; `cw_quality_scene_scale`). Every
  game builds and passes the smoke, art and prompt checks on it, draws its HUD crisp at Resolution 50% (each
  game's bot through romdev), and Destructo's water gate passes (v0.47 changed the water).

## 0.27.1

- **Destructo**: new launcher art from the current game: the cover, background, title, icon and four screenshots show the lattice Iron Lady toppling, the Hoover Dam water spill, the Mount Rushmore faces and the Taj Mahal dome opening, in the new light.

## 0.27.0

- **Destructo**: a new light. The sun casts two cascades of soft shadows that now reach the whole scene (they cast nothing before), a sky light fills the shade with blue instead of black, ambient occlusion darkens the carved faces and crevices, a film grade gives warm highlights and cool shadows with more colour, and grass blades catch the sun. Each setting is tuned on its own. About 1 to 1.7 ms more GPU a frame.

## 0.26.1

- **Destructo**: a far finer Iron Lady. The Eiffel Tower is now a real lattice of 7 cm members (5,463 welded, was 1,057): corner posts, X-braced panels, arches clear of the ground, a deep frieze, platforms with railings, a tapering shaft, the cabin and a tapered spire. It still topples from every leg-out test (one shot sends it 1.4 m sideways in two seconds), and steel heaps now crumble away sooner after they land. A blind judge names it at 96 and 97 percent.

## 0.26.0

- **Destructo**: richer surfaces on every landmark. Stone, wood and steel bricks carry normal maps in four variants, and each stone brick carries a surface baked from the landmark's model: shading in recesses and carved relief, grime by height, long stains, light and dark courses, strata on the Hoover Dam cliffs and rust on the Iron Lady, all of which stays on a brick when it falls. The four Mount Rushmore faces now read in the surface itself (brows, noses, lips, cheeks, jaws). Frame cost is unchanged.

## 0.25.2

- **Destructo**: Hoover Dam and Mount Rushmore have 14 shots, 4 heavy balls and 4 charges. The aim path now keeps its size on screen from a high camera and ends in a large gold marker where the shot reaches the structure, so you can see where you are aiming on every level. The Hoover Dam camera sits lower and farther back, and the Mount Rushmore camera backs off so all four heads fit.

## 0.25.1

- **Destructo**: the Eiffel Tower and the Willis Tower topple again. Since 0.23.0 their finer bricks made them sit down when a leg or half the footing was shot out; both are back at their earlier brick size. One shot at one leg of the Eiffel Tower now sends it 3.35 m sideways in two seconds, and the Willis Tower with half its footing out tips 17.8 degrees. Mount Rushmore keeps its fine heads.

## 0.25.0

- **Destructo**: Mount Rushmore, rebuilt. The four heads are laid in 12 cm bricks, bonded and hollow so brows, noses and chins hold their shape, in pale carved stone on a rough granite mountain built of large blocks. The level opens straight at the faces from slightly below, and a charge on a head breaks it away as a lump that shatters. A blind judge names it from the frame alone in both views (97 and 93).

## 0.24.1

- **Destructo**: the Hoover Dam's gorge walls have larger blocks in their cores, 12% fewer static chunks with the same silhouette and surface.

## 0.24.0

- **Destructo**: the Hoover Dam, with water. A thin curved concrete wall with four intake towers and a crest road stands in a tall dark gorge with a big lake behind it, opened from downstream and above. Break the wall at its foot and the lake spills through the breach at once: a slow-motion beat on the first water, mist and spray, a surge that spreads down the valley as a flood wave, the lake dropping as it empties, and the pad rumbling with the flow. The water is up to 240 small bodies that run downhill, lose their energy and pool, and do not count toward the settle wait. A level can now set its own camera framing.
- A blind judge names the dam from the frame alone in both views, and a check with the dam left standing makes no water.
- Landmarks: 42 in all.

## 0.23.0

- **Destructo**: finer bricks. Thirty-one landmarks are built of smaller bricks, 1.4 to 2.8 times finer cells: Saint Basil's has 8,048 bricks (from 2,804), the Kremlin 4,642 (from 997), Notre-Dame 5,794 (from 2,093), the Pantheon 5,864, the pagoda 6,052, the Japanese castle 5,646 and the Taj Mahal 3,696. A blind judge still names each from the frame alone; Harbour Sails, Angkor Wat, Hagia Sophia, the Blue Mosque and the Atoms keep their older cells because the finer ones failed a check.
- **Destructo**: a collapse no longer spikes. Removing a piece from a bonded structure made the physics recompute its mass once per member, so a welded landmark's worst collapse tick took 100 to 700 ms; it is under 20 ms now (Notre-Dame 11.9 ms, Pisa 19.2 ms the worst), and the mean physics step on every landmark is under 7.3 ms on the check machine.
- Known: the human-like full sweep clears Golden Span 4 of 8 and Brooklyn Bridge 6 of 8.

## 0.22.0

- **Destructo**: only famous places. The four tutorial levels and the 26 other invented buildings are gone, and so is Christ the Redeemer; the level select is 41 real landmarks ordered by difficulty, and the first level is the Leaning Tower of Pisa with the first-use hints. The store art and screenshot show landmarks.
- Tests: the performance, hitch and flash checks run on the Taj Mahal; a newcomer clears the first level within par plus 4, with a control whose aim is far worse.
- Known: Notre-Dame's collapse costs more than a frame's budget on the check machine and the human-like full sweep clears Golden Span 4 of 8 and Brooklyn Bridge 6 of 8.

## 0.21.0

- **Destructo**: four more landmarks, 42 in all: Stonehenge (the lintels now lie along the ring instead of across it), Saint Basil's Cathedral, the Red Tower in Tokyo and the Temple of Heaven. Each is named from the frame alone by a judge in both views. Mount Rushmore is built of 0.28 m bricks now: the physics step averages 6.5 ms over four seconds after a charge, down from 12.4 ms.
- The block builder keeps aligned parts clear of each other, so seams no longer overlap, and the level check prints where the first floating block is.
- Held until they read at a glance: the Great Wall, Machu Picchu, Petra, Hoover Dam, Abu Simbel, the Sydney Harbour Bridge, Twin Spires, Neuschwanstein, Borobudur, Shwedagon, the Sagrada Familia and Mont Saint-Michel.

## 0.20.0

- **Destructo**: eight more landmarks, 38 in all: Mount Rushmore (four heads in pale stone on a rock mountain, built of 0.2 m bricks), the Kremlin (the Spasskaya tower with its clock faces and star, green tents, gold onion domes, a bell tower and the swallowtail wall), the Brandenburg Gate (six columns, a flat attic and the horse-drawn chariot), the Colosseum, the Atomium in Brussels, a Japanese five-storey pagoda, the Gateway Arch in St. Louis and the Brooklyn Bridge. Each is named from the frame alone by a judge in both views. The Colosseum no longer has the overlapping blocks that held it back.
- Held until they read at a glance: the Great Wall, Machu Picchu, Petra, Hoover Dam, Abu Simbel, the Sydney Harbour Bridge, Saint Basil's, Twin Spires, Stonehenge, the Red Tower and Neuschwanstein.

## 0.19.0

- **Destructo**: the levels are real landmarks. The level select is four tutorial levels (First Stones, Lookout, Garden Wall and Step Shrine), then 30 landmarks ordered by difficulty and region; the invented levels are out of the list. Eleven landmarks are new: Notre-Dame, the Pantheon, the Arc de Triomphe, Hagia Sophia, the Blue Mosque, a Japanese castle, a stacked pagoda-segment tower in Taipei, a skyscraper with a crown in New York, a television tower in Berlin, a sail-shaped hotel in Dubai and a bridge in San Francisco, all built of small bricks from openly licensed models (CREDITS names each model, its author and licence) and chosen by a check in which a judge shown only the frame must name each one. Mount Rushmore, Brooklyn Bridge, the Kremlin, the Brandenburg Gate, the Great Wall, Machu Picchu, Mont Saint-Michel, Neuschwanstein and a few more are built but held until they read at a glance.

## 0.18.0

- **Destructo**: a structure that loses its footing now falls over as one solid piece. Before, a tower shot in the foot mostly sank into itself and crumbled in place. A bonded structure is now one rigid body: when its supports go it is released as one body and gravity topples it, then it breaks into slabs and rubble where it lands. With half of the Eiffel Tower's footing out, two seconds later it has gone sideways 9.4 m and tipped 61 degrees; the Black Tower tips 44 degrees, the Empire 45, Pisa 19.
- **Destructo**: supports share the weight. Each leg, base column or shaft segment of a frame carries a share found from where the structure's weight sits over its supports and is good for 1.25 times that share; when one is destroyed its share moves to the rest and any support now over its limit crushes, until it is stable or the footing is gone. One shot at one leg of the Eiffel Tower crumbles the leg, and the tower leans toward it and goes over; a building with many columns absorbs the loss of one.
- **Destructo**: a blow's push on a standing mass is capped at the blow's own momentum (a 50 kg ball was shoving a 25 tonne tower several metres a second), so one shot no longer shoves a tower over or releases it by push. Liberty, the Sphinx, Iron Lady and Pisa have retuned ammunition, pars and marks.
- A check fires the launcher's own shot, heavy ball and charge at the foot of eight landmarks, and counts every tick in which something stands or sleeps with no path to the ground.
- Build: Lanternfall's world check runs as four parallel jobs, so a run takes about 12 minutes.
- Known: a charge at one leg topples the Eiffel Tower by four seconds, not two; the Needle and the Black Tower shot at one base column still fall; the Taj Mahal does not topple from a shot at its foot.

## 0.17.2

- **Destructo**: structures no longer hang in the air. Forcing a piece of rubble to sleep put its whole connected group to sleep, so a tower or a statue's torso could stay motionless with nothing under it; forced sleep now applies only to rubble lying within 0.7 m of the ground when fewer than 120 bodies are awake, and a frame released out of balance keeps a lean as it falls. A check fires the launcher's own shot, heavy ball and charge at the foot of eight landmarks and counts every tick in which something stands or sleeps with no path to the ground (the old rule fails it with 508 ticks).
- Known: a tower shot in the foot still mostly sinks into itself instead of toppling; the fix, a released mass falling as one rigid body, is next.

## 0.17.1

- **Destructo**: a level start never freezes the screen. While a level builds, the dimmed last view stays up with a spinner turning on the real clock, "Building <level>", a progress bar and a phase line, and Back cancels. The level's drawing is built a slice a frame behind the screen, the replay's second world is never waited for, and the first level builds under the engine's progress bar at start. A check records the frames of a start and fails if the spinner stops, the text goes, the progress goes back or a frame takes over 100 ms; the old blocking path fails it with frames of up to 28 seconds.

## 0.17.0

- **Destructo**: the game answers you at once. After a shot leaves, aim, ammunition and fire were shut until the rubble settled (2.7 seconds here, up to 15 on a big collapse) and presses in that time were thrown away. Now the aim opens when the shot strikes, the launcher turns and ammunition changes in the same tick, and a fire press made while the shot is in the air is kept and fires the moment the aim opens, with a "FIRE QUEUED" mark. The ammunition icon and the menu tile pop in the frame of the press.
- **Destructo**: the sticks aim properly. They had a dead zone so wide that a push of 0.3 did nothing and a push of 0.6 turned at 0.07 rad/s; now the dead zone is 0.12 with a smooth curve, a full push turns at 1.2 rad/s (it was 0.58), speed eases on and off, a held d-pad sweeps half a radian in 0.62 s, and a one-frame tap is under a tenth of a degree. The orbit is eased. The level select repeats a held direction (40 tiles a second where it was 4) and wraps. Free fire has 99 of every ammunition.
- **Destructo**: the barrel kicks back when it fires, and the winning blow runs the world at 0.35 speed for a third of a second, eased in and out, with input never held (no shake, no flash).

## 0.16.1

- **Destructo**: the tall landmarks are no longer a thin shell over a solid core. A tower is a perimeter wall, a floor
  slab every 3.6 m, a hollow shaft and columns; the Needle and the CN Tower have hollow tubes with landings; the
  pyramids' cores are made of normal bricks. When the footing goes a tower tips about the edge of what is left and
  pancakes into rubble within about three seconds; four seconds after half of the footing is broken, 83 to 99 percent
  of the blocks have moved more than a metre or are gone on every tall landmark (the Great Clock, Angkor Wat and the CN
  Tower included, which stood on their cores before). A tilting tower froze mid-fall because sleeping one piece of
  rubble put its whole connected group to sleep; it now sleeps only when fewer than 120 bodies are awake.
- Build: the native logic checks run as parallel jobs beside the carts job, and a tag skips them, so a release takes
  about a third of the time.

## 0.16.0

- **Destructo**: buildings no longer flop. Structures are rigid chunks held up by what is under them, with no weld
  joints, which had made everything above a broken leg hang together as one soft jointed chain. A chunk that loses its
  support falls as a solid piece and breaks apart where it lands hard: knock out one leg of the Eiffel Tower and the
  top tips over rigid, breaks into sections and shatters into a heap (the largest piece four seconds later is a few
  blocks, nothing rebounds). Each shot leaves at its own fixed power: a shot at 62 m/s, a heavy ball at 40, a charge
  lobbed at 28. A stone crack or a steel groan sounds when a chunk lets go, a thud, chips, dust and pad rumble follow
  when it lands, the charge's boom is delayed by distance, and the camera follows the collapse. The pars and marks were
  retuned for the fixed power.
- Known: the cores of the landmarks are still solid blocks inside their walls, which is why a tower's middle is too
  rigid and why some landmarks do not fall to a leg-out; real floors, columns and hollow cores are next.

## 0.15.1

- **Destructo**: the next level is ready the moment you accept it (50 to 130 ms; the Empire took over 15 seconds, Iron Lady over 5). It builds in the background while the results card shows, and the level-select preview builds in the background too; if you accept before it is done, the card says "Building the next level". The replay's second world builds in the background. The depth-fighting scan of the whole field is gone (the grass shares no planes), which was most of the load time. A level with nothing built ahead of it, such as the first after the title, still waits for its build.

## 0.15.0

- **Destructo**: simpler controls and a clear screen. Aim with the left stick or the d-pad, go round the structure with
  the right stick, South fires, West and East change the ammunition, North toggles the wide view, Start pauses. No
  triggers or shoulder buttons are read, and there is no charging: each ammunition leaves at its own fixed power (a shot
  and a heavy ball at full speed, a charge lobbed). Mouse and touch aim by drag and fire by tap.
- **Destructo**: the play screen shows only a share bar, the shots against par, the crystal count and the ammunition;
  the level name and par show at the top for three seconds; one hint at a time shows and goes once the control is used;
  the result card can be skipped at once; the level select is four columns of large tiles with stars or a padlock.
- Known: the computer player needs more shots than par on Tower Bridge (7 against 5) and Angkor Wat (6 against 3) now
  that the power is fixed; the pars are being retuned.

## 0.14.3

- **Destructo**: the level ends the instant the last crystal is down. The results card and the stars show at once
  (before, a level was judged up to 7 seconds after the last crystal fell, with "Settling..." on screen), and the
  rubble keeps falling behind the card. The share the card shows keeps growing while rubble falls (up to 8 seconds), so
  the stars stay fair.

## 0.14.2

- **Destructo**: the landmarks stand rigid. They had swayed and leaned for seconds as the game began (the welded
  towers' tops moved a quarter to half a metre) because the engine holds a weld with a soft constraint that sags
  under the weight above it. Every landmark now starts asleep, in its design pose, and settles by nothing; the six
  that never fell asleep (Empire State, Tower Bridge and the Statue of Liberty among them) rest at once and cost
  nothing until hit. The check's settle bound is 2 cm for every landmark, with a control that runs the old soft
  welds and must fail.
- Bricks that cling together after a hit and a tower that shatters as it falls are written up and built behind
  switches that are off for now, because turning them on breaks the par of several levels.

## 0.14.1

- **Towers**: the cursor's P1 to P4 label no longer covers a deploy button. The pills and the label are laid out from
  measured boxes, the label sits just above the top pill (under the bottom one near the top of the screen), and every
  HUD element registers its extent so a cursor slides to the nearest clear place: two cursors a couple of tiles apart,
  the top row of maps 8, 10, 17 and 22 under the wave line, the side columns of maps 7, 13, 20 and 25, and the map
  banners are all clear. A check sweeps every slot of all 30 maps at 1 to 4 seats and three screen sizes. A second
  layout with one face-button diamond at the cursor is built behind a switch and is not the default.
- **Towers**: a long session no longer runs out of memory by map 16 (a map's lightmap was never freed when the map
  changed).

## 0.14.0

- **Destructo**: famous buildings of the world as the first levels, nineteen of them (the Great Sphinx, the Pyramids of
  Giza, the Taj Mahal, the Parthenon, Tower Bridge, the Leaning Tower of Pisa, the Statue of Liberty, Angkor Wat,
  Chichen Itza, the Moai, Big Ben's tower, the Eiffel Tower, the Space Needle, the Empire State Building, Willis
  Tower, Burj Khalifa, the Sydney Opera House, Christ the Redeemer and the CN Tower, some under lightly changed names),
  each scaled to the field with its proportions kept and built of thousands of small bricks of stone, steel, glass and
  brick that break the way their material does; most come from openly licensed 3D models (CREDITS names each). Each
  level opens on the view that identifies the building, and a check asks a judge who sees only the frame to name it.
  Six more are built and held until they settle cleanly. The orbit camera (right stick, or hold East with the d-pad)
  aims round a structure. Every Destructo check now runs as a cart through romdev, including frame timing.
- **Destructo**: the city skyline no longer crashes a cart (the depth-fighting scan's grid is bounded by memory); the
  physics steps on at most 12 threads (faster on a busy machine); a pad counts as a seat's input in versus; the quarry
  is brighter; the explosion's scorch mark is named before it enters the scene, which the engine now needs.

## 0.13.2

(0.13.1 was tagged on a commit whose CI run was dropped, and 0.13.2's build failed on Destructo's scorch mark under
engine v0.46.2; 0.14.0 is the first release of this line that builds.)

- **Lanternfall**: the camera no longer whips a bright room in and out of view when the hero hops through a doorway
  (it had reached five flashes a second at the top of the screen); the view takes a new room after a moment and glides
  to it at most a screen in 0.6 s, and the far layers ease with it. The backdrop's trees stand on painted ground in
  every layer, with a grounding check across every level.
- **Launcher art**: the Lanternfall cover shows the lamplighter large beside a lit lamp, the Towers cover the crimson
  crowd on the volcano map, and Ember Throne's logo is bold in two lines. All art is now taken from the carts through
  romdev.
- **All games**: a photosensitivity sweep runs a busy stretch of every game on GL and WebGPU, with a strobe control that
  must fail.
- **Towers**: the snow maps (16 to 20) no longer crash the cart when they load (the depth-fighting scan's grid is now
  bounded by memory). Every map's ground and pools draw with their baked light on a cart; they had drawn unbaked
  because one maths function differed by a hair between the machine that bakes and the cart. The explosion's scorch
  image is in the pack. A timing check runs every map through romdev on GL and WebGPU with bounds per map.
- Engine v0.46.2: physics uses at most 12 worker threads by default, decal images load when they are assigned, and the
  photosensitivity check runs through romdev.

## 0.13.0

- **All games**: each cover carries the game's name across the top, large enough to read at a launcher's grid size,
  and each cart's manifest names its title (Destructo, Ember Throne, Jewels, Lanternfall, Pinball, Skate, Towers).
- **Launcher art**: Destructo, Ember Throne, Jewels, Lanternfall, Pinball and Skate are taken again from the games as
  they are now. Jewels and Skate have bolder logos.
- Engine v0.45.0: the shell's menus take touch and mouse, a load without a seed asks for no extra draw states, and
  split screen's fog holds.

## 0.12.0

- **All 3D games**: no surface fights another for the depth test. Faces laid flush on others (props' parts, the rock's
  folds, signs and sidewalks, the pinball table's walls and cabinet, the buildings' trims) are layered apart when the
  game builds, so the speckle where they met is gone; surfaces that look the same where they overlap are left alone.
  A depth-fighting check runs in CI.
- **Characters**: athletic proportions across the cast, measured by a new check against an athletic adult (heads,
  shoulders over waist, limb taper, hands and feet at a real size):
  - **skate**: the four riders, with closer-fitting tees, sleeves and trouser legs.
  - **Ember Throne**: the cast's arms at full length and their sleeves and breeches tapered. The brute and the two
    bosses carry their size in the chest, back and shoulders.
  - **towers**: the ghoul, imp, shade and ogre rebuilt, with the ogre a muscular brute. Walkers keep a foot on the
    ground in every frame. The desert's crowd flags take the pale edge the snowfields have.
  - **Lanternfall**: the lamplighter, the knight and the Brass Sentinel; the statues are now carved figures.
- **skate**: the board is a street deck in its real proportions: a thinner deck with gentler kicks, low dark trucks,
  smaller matte wheels and open space under it. A check measures it from the game camera against a real board.
- **towers**: creeps and towers draw their upper faces first, so the scene costs less on GL and WebGPU with the same
  picture.
- Engine v0.39.2: GL's depth prepass writes depth only (Ember Throne's meadow costs less on GL), and romdev test runs
  wait as long as their frames take.

## 0.11.0

- **towers**: the difficulty is tuned for a first-time player. A boss is a lesson on the first maps and grows to its
  full strength by map 18; a wave's creeps are shared over the gates by the length of each road, so a gate beside the
  core no longer sends half the wave; the next-wave line names a kind the map has not sent before, with what it does
  and the tower that answers it. Maps 1 to 7 are held by a newcomer alone or in two, the later maps still challenge,
  and maps 11 to 30 are retuned so that skilled players lose some of the core again. A newcomer check plays the maps
  the way a first-time player does, from what the screen shows and at a person's pace.
- **towers**: a late crowd reads as figures, not a carpet: each creep has its own size and shade and is drawn a little
  apart from those it overlaps, and ogres and bloaters carry a rim of their own colour inside their outline, at no GPU
  cost. The crowd check judges the volcano's ogre wave.
- **towers**: lightning swells in and fades instead of cracking: the coils' strikes and the storm's, with their light,
  so the volcano stays at two flashes a second at most in any part of the screen (WCAG allows three). A strike is never
  cut off for a new one.
- **skate**: riders can ride goofy (right foot forward): a STANCE row on the setup for the first player, up or down on
  the stick for the others, saved with the skater. Every trick has its goofy twin. The bots ride both stances.
- **skate**: a landing missed by a little at a pace that can be run off is a run-out: the rider steps off, runs after the
  rolling board and steps back on, instead of falling. A run-out check holds it.
- **pinball**: the mirror ball reflects as sharply as in 0.10.0 on the new engine.
- Engine v0.39.0 (through v0.38.9): no screen shake anywhere (none of these games used it: hits are felt in the pads).
  The pinball and Ember Throne perf gates bound each window's mean frame time.

## 0.10.0

- **Ember Throne**, the combat round: a blow that lands holds both fighters still for a few ticks, knocks the foe back
  by its weight, and turns toward a foe locked on or just in front. Shields block blows from the front, and a heavy
  blow knocks them aside. Foes have poise that breaks into a stagger, open for a critical; after a big blow they are
  spent for a moment. Foes circle to the player's flank instead of lining up, bosses chain blows in their later phases,
  and lurkers wait half risen in the marsh pools until the player comes near. A flask refills after a fight is won.
  The pad rumbles on landed blows, hurts and kills; no screen shake.
- **Ember Throne**: the camera no longer stops inside pine and fir needles. The canyon, keep, heights, marsh and
  meadow are relit (exposure, sky fill, mist, a blue morning haze over the meadow's far hills). The shield bearer's
  shield is a plank that falls with its bearer.
- **Ember Throne**: the look check judges each region on five frames across the journey instead of one.
- CI: the carts job may run for 90 minutes.

## 0.9.0

- **Lanternfall**, the combat round: a blow that lands holds the world still for a few ticks (longer for a kill or a
  blow taken) and knocks the enemy back by its weight, a downward strike restores the dash, and the pad rumbles on
  blows, kills, hurts, plunges and wardens. Four new enemies, each with one idea: the puff (only a blow from above
  hurts it, and it bounces you), the dropper (falls on whoever passes under), the burrower (a ripple that follows you
  along the floor, then bursts up) and the eye (charges a beam along its row). Each region has its own cast, and
  creatures struck down stay down until a rest. Relics give charms in the order found (longer reach, a cutting dash,
  a sprint, energy from kills, a faster mend, a higher pogo, a thorn ward, quicker strikes, a twin spark, a wider
  plunge, a second wind). Item rooms lock as trials with two waves of guardians first. Wardens stagger at half health
  and come back with a second phase. A fall leaves your lantern's flame where you fell, holding some energy to take
  back. A dash can leave straight off a wall you cling to.
- **Lanternfall**, the world round: hand-shaped set pieces among the rooms, a new order of abilities (the grapple line
  third, climbing after the Brass Sentinel, the plunge in the Forge), 13 shrines, grapple shortcuts in four early
  regions, cracked caches in plain sight, and keepsakes to find past the last charm. The map marks finds seen but not
  yet reached, map stones, landmarks, region names and the gates by the ability each wants; map stones and tablets
  show cards. Each warden's arena is laid out for it, and the opening room leads along its floor with lamps showing
  the way.
- **Lanternfall**, the painted look: every creature is painted in code with shaded forms, fur, chitin, scales and
  glowing eyes, with a rim of light on top. The rock carries painted features (boulders, cracks, strata, fossils,
  roots, crystals, ore, gears, shells, moss), each region has its own rock, values and accent, and every ledge in the
  air hangs on chains or brackets or stands on posts. Painted props fill the rooms (lamplighter statues, garden
  trees, crystal clusters, machines, shelves of books, mushrooms, ferns, grass and flowers that sway in the wind),
  trees with bark and leaves stand in the background, arches carry masonry and ivy, and landmarks and tablets are
  painted from the world's own props. The strike sweeps as a soft ribbon behind the lantern and the dash as a smear.
  Nothing strobes: hits and hurts swell and fade.
- **Ember Throne**: a new cast. All eight figures are rebuilt from CC0 base characters and fantasy outfits with
  generated plate, helmets, crowns, horns, robes, coats, capes and masks, larger heads and hands, and baked skin.
  Every figure has a rim light, foes' outfits are tinted by region, each figure picks its level of detail from its
  size on screen, and feet are planted at each kind's ankle height.
- **Destructo**: a level is judged once its world has gone still, never while the structure is still falling. Stars:
  one for every crystal down, two within par, three within par with the structure brought down to its mark, and the
  HUD shows the share down live. The last shot can be watched again as an instant replay that follows the shot and
  turns slowly round the impact in slow motion. Free fire on the select screen (every structure, endless ammunition,
  never judged), a party mode with steady hands, and powder kegs that set each other off. Grass, the sea drawn as
  water, a dust plume that drifts downwind over a collapse, a mark on the ground where the structure came down, raw
  insides on broken pieces and ambient occlusion from Medium up. Every light swells and fades instead of flashing.
  A cannonball now strikes with a burst that crushes and drives back the blocks around it, so every level can be
  cleared with ordinary aim; the aim path runs all the way to the structure's face, and par is retuned to a steady player's
  shots. In co-op any pad plays any turn, and in versus and party any pad can play a seat left idle for eight seconds of
  its turn. A new check has eight simulated players, aiming from what the screen shows with a person's aim error and
  reaction time, clear every level with its ammunition.
- **jewels**: bigger clears. Every cleared stone gives off light, and the stones around it are lit by it; the velvet
  keeps a soft afterglow where a clear was. Clears throw shards, dust, rays and a shockwave, and the room warms as a
  cascade escalates, ending in a sweep at its climax. Fixed: the emerald cut's flat top no longer fills white when
  one lamp lines up with it, and the finale's word sits in the panel instead of a band over the board.
- **skate**: the riders move like skaters. Feet stand where skaters put them on the deck, the push drives the back
  foot past the tail, carves lean as far as the turn asks, transitions load the legs, the ollie snaps the tail and
  drags the front foot up the grip, flips flick off the nose while both feet lift, grabs pull the board up to the
  hand, grinds and manuals sit over their truck, and a bail braces before the ragdoll takes over.
- **towers**: nothing strobes: lightning is one fork that writhes and fades, flash lights, beams, coils and mortars
  swell in and fade out, hit creeps warm and cool, and the core count glows instead of blinking. The third level
  comes in two branches (arc, blizzard, prism and napalm on the second). Towers help each other: blasts shatter frozen
  creeps, beams heat faster on slowed ones, lightning leaps farther off chilled ones. Each player has a storm to call
  during a wave. Killed creeps tip over and sink with a coin rising, each wave's banner says what each player did,
  tower levels show at a glance (halos and a crown), crowds stay readable against any ground, and cloud shadows drift
  over the map.
- **pinball**: nothing strobes: every light eases to what the rules want, pulsing lamps are slow smooth waves,
  flashers rise, hold and fade and wait 1.3 s before firing again, a spinning spinner is drawn turning slowly, and the
  dot display slides and brightens instead of blinking. Each table has its own modes, toy and wizard mode (Coral
  Deep's creature, Star Foundry's magnet and forge ring, Neon Fair's high striker), with roving shots, hurry-ups,
  target sweeps and two-ball modes, and five goals a table beyond the score, kept as stars. A painted backbox per
  table, illustrated playfields, and the machine standing in a dim arcade room between the other two tables. The pad
  rumbles for flips, coils, drains and jackpots. A ball that stops on a ramp's crest now rolls back instead of
  balancing there for ever.
- Engine v0.38.8: Lanternfall's flipped and turned painted parts are lit the way they face (the engine turns a 2D
  sprite's normals with its rotation and flip), so each part is painted once instead of once per facing. Every game's
  flash check now uses the engine's flash analyser (WCAG 2.3.1 and the Harding test's thresholds) with a strobe
  control that must fail.

## 0.8.2

- pinball: the flippers answer much faster: they move on the simulation step of the press, reach full stroke in
  about 24 ms and drop back in 28 ms, and the ball is drawn on the same step as the flippers instead of one behind.
  The flippers stay live until a tilt, through the drain and the bonus count, so a ball that has slipped past them
  can still be saved. The face buttons flip (west and east) and the south button flips both once the ball is in
  play; the prompts show them.
- jewels: a second look round. Each stone is cut and coloured against real gems (ruby with dark quarters and cross
  flashes, golden citrine, royal sapphire), glints come only from real highlights and twinkle as the stones turn,
  the tray is tufted velvet with a soft dish under each stone and the light each stone throws pooled in its shadow,
  and the frame is lacquered walnut with gold inlay. It also draws faster.

## 0.8.1

- Engine v0.38.3. Every cart now carries its real version in its manifest: a release cart says this release's
  number, any other build says the last release plus a development mark. A tagged release builds its own carts and
  checks every cart's version against the tag before publishing.
- Ember Throne's terrain draws its second paint layer more cheaply (from the engine), and sun shadows skip their
  lookups where no shadow cascade reaches.

## 0.8.0

- **jewels** (working title): a calm, untimed match-three for one player, readable from across a room, played with a
  pad, touch or a mouse. Nothing on the board moves unless the player moves it: an illegal swap springs back for
  free, the hint is free and unlimited, and a deadlocked board reshuffles itself. The stones are cut gems traced per
  pixel through their real facets (round brilliant, emerald cut, princess, pear, marquise and trillion), with Fresnel
  reflection at every face, total internal reflection, colour that deepens with the stone's thickness, fire from
  dispersion and the light they cast onto the velvet tray. Clears burst bigger with more stones, and cascades build
  with each step.
- skate: the bots skate from line to line through the parks instead of stepping back to a start, vert airs leave at
  the ramp face's angle, the plaza has more contrast, the sunset sky no longer bands, and the camera swings back
  behind the rider after a vert landing.

## 0.7.0

- **Ember Throne**: a third-person action adventure for one player, with a drop-in second. The wanderer gathers the
  Sun Key and the Moon Key to open the Ember Keep and face the Ember King on his throne. One connected world of eight
  regions (a ruined hollow, an amber canyon, a marsh with a temple, a glowcap wood of giant mushrooms, frozen heights
  over a gorge, a lantern-lit underway, a windward spire and the keep), with switchbacks, a cliff path over a timber
  bridge and doors that open back as shortcuts. Lock-on sword fights with a roll, stamina and a healing flask, a maul
  that breaks cracked stone, three bosses with phases, shrines that save. Feet stay planted on slopes and stairs, the
  camera keeps out of walls and foliage, and every prop is checked to stand on the ground. Known open item: the
  meadow runs over its speed target on the reference laptop GPU and is being worked on.
- explorer is now **Lanternfall**: the game, its carts (lanternfall.wasc), apps, tests and launcher art carry the new
  name. Holes through floors now read as drops into dusk and mist with roots, icicles or chains hanging over them;
  the undersides of overhangs are shaped instead of stepped; each of the 13 regions has its own rock (sandstone,
  slate, basalt columns, ore veins, crystal, coral, cloudstone and more). It also runs faster (3.0 ms on GL and 3.2 ms
  on WebGPU on the reference laptop GPU, from 3.4 and 3.8). Saved games from the earlier name do not carry over.
- towers: pools are real water with irregular shorelines, deep colour, drifting sky glints and wet banks; lava pools
  have a cracked crust with heat shimmer; rock outcrops are faceted cliffs with strata, boulders at the foot and moss,
  sand, snow or ash on top. Two players' cursor labels no longer overlap.

## 0.6.0

- demolition is now **Destructo**: the game, its carts (destructo.wasc), apps, tests and launcher art carry the
  new name.
- Destructo gets a new look: towers of warm stone, timber and plaster under slate, terracotta and copper roofs, and
  the tall towers carry stepped roofs too, so the roofs and finials show from the launcher. Trees of layered foliage
  (oaks, birches, spruces, snow-laden firs, date palms, two species a setting) sway in the wind; the ground rolls past
  the field of play and the yard and track are worn into it. The desert is pale dunes under red mesas instead of an
  orange haze, and the snow is shaded. The look check passes on all seven settings, the biggest collapse stays under
  4 ms on GL and WebGPU and the biggest blast inside one 60 Hz frame.

## 0.5.0

- **demolition** (working title): a physics destruction game for one to four players taking turns, in co-op or
  versus. Towers, walls, bridges, castles, scaffolds, stepped pyramids and keeps of wood, stone, glass and steel,
  from 44 to about 2500 blocks, stand in settings of their own (highlands, a meadow, a river gorge, sea cliffs, a
  quarry, desert mesas, a snowy pass). A launcher fires shot, heavy balls and charges at glowing crystals hidden
  inside; blocks wear down and break into pieces, dust and shards, and whole structures come down on the engine's
  3D physics. 30 levels, each checked to stand still for a minute, float nothing and fall to the bot within par.
  The biggest blast stays inside one 60 Hz frame (the effects are prepared at load and breaks are spread over a
  few ticks).

## 0.4.0

- towers gets a new look: paths are smooth curved roads of real materials per theme (packed earth with a brick
  curb, slate cobbles, sandstone flags, frozen gravel, ash crust, pale flagstones) with a clear step from the
  ground, wide stretches become paved plazas, rock outcrops are shaded stone, pools have shorelines and lava a
  cooling crust, and each tower type has its own colour again (bolt violet, frost ice-cyan, beam rose, mortar
  amber) with the owner's colour on its trim. Creeps walk only the drawn road, a creep stuck for 6 seconds is
  moved on, and maps 11 to 30 are retuned for the new layouts. A look check measures each
  theme's contrast and fails stair-stepped edges.

## 0.3.1

- skate looks better: thinner, bluer haze so every horizon keeps its colour; cars with real shapes and glass
  cabins, trees of layered foliage, sunlit cumulus clouds, building fronts with sills and piers; the street plaza
  gets planters with trees, granite bands, benches and longer morning shadows; a colour grade per park. Its
  four-way split runs faster on WebGPU (3.6 ms, was 4.1 ms), and the Big Sky practice rail moved clear of the
  giant quarter pipe.

## 0.3.0

- **explorer** (working title): a 2D exploration platformer for one player with a drop-in second. One
  underground world of 13 regions and 356 rooms; seven abilities (a second jump, a dash, a wall climb, a plunge,
  swimming, a grapple and a spark) each open ways that were shut. Six wardens, shrines that save and carry you
  between them, and a map that fills in as you explore. Drawn with the engine's 2D renderer: lit sprites, soft
  shadows, bounced light, light shafts, bloom and a colour grade per region. CI checks that the world can be
  finished in ability order with no dead ends, every pickup reached and nothing floating, and that the jump's
  feel stays in its bounds; a bot plays it through to the end.

## 0.2.0

- **skate** (working title): skateboarding for one to four players in split screen, in five original parks,
  each with its own setting to the horizon and its own time of day, lightmapped. Flips, grabs, spins, grinds and
  manuals with balance meters link into combos; a bail loses the combo and drops the rider as a ragdoll. Free
  skate, a two-minute score attack with goals and a saved high score per park, and score battles. The board runs
  on the game's own controller, substepped and swept against the park's triangles; every park is checked in CI
  (nothing floats, the bakes are current, a bot skates every line).

## 0.1.1

- Engine v0.38.0 (its 2D rendering releases v0.37.0 and v0.38.0: tiled 2D lights, soft shadows, a high-range
  2D post chain, 2D bounced light, water, GPU particles, sprite instances, colour grading). towers and pinball
  draw the same as before.

## 0.1.0

The first release: two games on engine v0.34.3, each as one dual GL + WebGPU cart.

- **towers** (working title): a tower defense game for one to four players on one screen. Each player builds
  in their own share of the map; the four towers (chain lightning, frost, a ramping beam, a mortar) build from
  a diamond of the face buttons round the cursor and upgrade to level 3. Waves of eight creep kinds grow into
  the thousands on the crowd system, with a boss every tenth wave. 30 maps in six themes (meadow, autumn,
  desert, snow, volcano, crystal) with six path layouts, merging gates, rock and pools, each baked and checked
  in CI: the path, the pads, fair shares, grounding, the bake and a two-bot clear.
- **pinball** (working title): three original tables (Coral Deep, Star Foundry, Neon Fair) with ramps, pop
  bumpers, drop targets, four modes each and a three-ball multiball, for one to four players taking turns.
  The ball runs on the game's own solver, swept and substepped so it never passes through a wall or a flipper
  at any speed. A dot display, a chrome ball with reflections, lit inserts and flashers, nudge and tilt, and a
  saved top five per table.
