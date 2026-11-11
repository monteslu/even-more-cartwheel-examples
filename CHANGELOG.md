# Changelog

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
