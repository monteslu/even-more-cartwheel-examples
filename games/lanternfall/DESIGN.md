# Lanternfall: design

A 2D exploration platformer. One great underground city, cut into rooms on a grid of screen-sized cells, thirteen
regions around a sealed heart. The lanterns of the old city have gone out; the hooded lamplighter wakes the five
beacons (the wardens' rooms) and then the heart beneath them. Every ability found opens a way that was closed
before, so going back is progress: earlier dead ends become shortcuts and side rooms with upgrades.

## The world

`src/worldgen.c` builds it from a seed (`tools/worldgen.c` writes `assets/world.bin`):

- **Regions** sit on a 60 x 34 cell grid (`src/regions.c`), each with its own palette and material (stone, earth,
  crystal, brass, coral, cloudstone) and its own music mood.
- **Links** between regions are gate rooms: two cells split by an obstacle that one ability crosses. The obstacle sits
  in a tunnel so nothing reaches over it from above:
  | gate | obstacle |
  |---|---|
  | second jump | a wall six tiles high (one jump rises 4.4) |
  | dash | an eleven-tile thorn pit under a low ceiling (the second jump alone carries about 10.8) |
  | wall climb | a sheer wall eleven tiles high |
  | plunge | a cracked floor between the two cells (only a plunge from above breaks it) |
  | tide charm | a flooded passage from floor to ceiling (without the charm the cold pushes you back out) |
  | grapple line | a 24-tile chasm under a beam of anchors |
  | the seal | the heart's doors, open once five wardens have fallen |
- **Set pieces** per region: the warden's arena with its prize behind a sealed far door, item rooms, save shrines,
  side branches behind a later ability's gate (the upgrades that reward backtracking), and hidden walls (a strike
  breaks them) with relics behind.
- A **spine** of cells joins every doorway a region needs, branches grow off it, and the cells are cut into rooms of
  one to three cells, joined by doors in a spanning tree plus a few loops.
- Each room is **carved**: walls, door openings, ledges under doors above the floor, ladders of one-way ledges up to
  them, the floor's steps, thorn pits and pools, then its **composition**: the ceiling's line rolls, rounded masses
  hang from it, bulges grow from the side walls (shelves, overhangs and the alcoves between them), islands of rock
  with tapered undersides and one-way ledges (some broken in two) float in the air, broken columns hang with their
  stumps below. Each piece is laid only where the air round it is open, never over a door, a ladder or a kept
  column. Then lamps, props and enemies, each placed on what holds it.
- **Set pieces** per region stand on level floor: machines with turning gears, pistons and furnace mouths in the
  Forge and the Clockwork Deep (pipes down from the ceiling, the thorn pits there are molten channels), trees in the
  gardens (the Bloom Gardens, the Rootway, the Fen grow thick with flowers), crystal clusters in the crystal regions,
  weed in the Tidal Halls, shelves in the Archive, lamplighter statues in the Lantern Gate and the Skyreach. Some
  halls in Glasswater and the Tidal Halls are flooded: wading pools from wall to wall.

The abilities come in this order: the spark (ranged, at the start), the second jump (behind the Moss Warden), the dash
(behind the Glass Mantis), wall climbing, the plunge (behind the Brass Sentinel), the tide charm (behind the Tide
Serpent), the grapple line. The Sky Warden is the fifth beacon; the Heart of Thorns is the end.

## The proof

`tools/check.c` (`lanternfall_check`) walks the world with the real movement code (`src/player.c`): a node is a tile a
player can stand on, an edge is a move (`src/reach.c`: a run-up, a jump held so long, a second jump at a tick, a dash,
a climb, a plunge, a swim, a chain of swings on the grapple line) played from three spots on the tile that must all
land on the same tile. It proves, stage by stage from the start with no abilities:

- the abilities come one stage at a time, in the order above;
- every place reached at a stage can get back to the start at that stage (no soft lock);
- every pickup and every warden is reached;
- every gate room's far half cannot be reached with only the abilities that come before its own, and can with it;
- every prop, lamp, pickup, shrine, anchor and ground enemy is held by the tile it says (nothing floats).

It writes the bot's route (`assets/route.bin`): the moves from the start through every pickup, shrine and warden,
the Heart of Thorns last of all.
`tests/lanternfall-world.mjs` checks both shipped files are current, and runs five broken worlds that must each fail
for their own reason.

## Movement

Measured by `tools/feel.c` (`tests/lanternfall-feel.mjs`), each with a control that turns the feature off and must fail:

| | |
|---|---|
| held jump | 4.4 tiles; a tapped one 2.0 (letting go cuts the rise: gravity triples) |
| apex | lighter gravity near the top of a held jump |
| coyote time | 6 ticks: a jump still works just after running off a ledge |
| jump buffer | 8 ticks: a jump pressed just before landing jumps on landing |
| run jump | 7.1 tiles; with the second jump 10.8 |
| dash | 4.8 tiles in 10 ticks, once per airtime |
| ceiling corners | a head that clips a corner is carried round it |
| dash off a wall | 4.8 tiles straight off a wall you cling to (climbing and the dash chain) |
| sprint (a charm) | a dash held on into the run keeps going at 1.45x; a jump keeps the speed |

Wall climbing clings when you hold toward a wall in the air, climbs on up and down, goes over the top, and the dash
throws you straight off it. The plunge
is down + dash in the air. In deep water the tide charm swims; without it the cold sends you back to the last firm
ground. The grapple line catches the nearest anchor overhead, swings, reels on up and down, and lets go with a jump.

## Combat

A strike (west) in front, up or down (in the air; a downward strike bounces off what it hits, thorns too, and gives the
dash back), and cuts small thrown things out of the air. A blow that lands holds the whole world still a moment (4
ticks, 6 for a kill, 7 for a blow taken: both sides feel it), throws the enemy back about a tile (further for a kill,
less for a knight, never off its ledge) and pushes you back a little; presses made during the hold are kept. Each blow,
kill, hurt and plunge rumbles the pad; the camera never shakes. The spark (north) costs energy; strikes refill it and it creeps back on its own.
Down + spark on the ground mends a leaf of health for two energy. Enemies show their tells: the crawler rears up
before it charges, the spitter swells, the moth's wings flare before it swoops, the hopper crouches, the knight raises
its spear (and turns strikes from the front with its shield). A strike breaks off an attack in progress. Four more,
one idea each: the puff drifts, soft (a touch bumps you, landing on it bounces you), then every puff swells and
bristles with thorns on one shared breath; only a strike from above hurts it. The dropper hangs from a ceiling,
shivers when you pass under, falls and bursts, then lies stunned. The burrower is a ripple of dust under the floor that
follows you, stops, and bursts up where it stopped; then it is out and open to blows. The eye charges while a line
shows the row it will burn, then burns it.

Each region has its own cast of four kinds (worldgen.c `CAST`). Creatures struck down stay down until you rest at a
shrine or fall. An item room that holds an upgrade (not a warden's prize) is a **trial**: its doors lock once you are
inside, two waves of the region's guardians come for you, and the find is yours once they fall.

**Charms.** Each relic found gives the next charm, in this order: the long wick (the strike reaches further), the
cutting dash (the dash hurts what it passes and nothing touches you in it), the runner's stride (a held dash runs on
as a sprint), ember harvest (kills fill energy), swift mend, updraft (a higher bounce), thorn ward (a blow taken bursts
out), quick hand, twin spark, quake (the plunge's shock reaches twice as far), second wind (once a rest, a killing blow
leaves a leaf). Relics past the eleventh give a leaf or a measure.

**A fall** leaves your lantern's flame at your last firm ground, holding a measure of your energy: go back and touch it
to take the measure back (a second fall moves it).

Wardens use a shared set of moves, each with a tell long enough to read (never under 26 ticks): a charge, a leap that
sends ground waves, a volley (the Moss Warden lobs spores at glowing marks), rain from marked spots, a sweep along the
floor (jump it) or at head height (stay down), a dive at a marked point, summoned helpers, eruptions at marked spots.
At half health a warden staggers (open to blows for a moment, its shots cleared), then fights its second phase: one
more move in its rotation and every third move chained straight into the next.

`tools/rules.c` (`tests/lanternfall-rules.mjs`) measures these rules on a small test world, each with a control that
turns the rule off and must fail: the hold, knockback, recoil, the puff, creatures staying down, the trial, the
warden's stagger and second phase, the first charm, the flame.

## Saves, travel, the map

Up beside a shrine rests there: full health, a save, and the shrine's waystone lit; the shrine's menu travels to any
lit shrine. The save keeps abilities, finds, the walls and floors broken, the wardens fallen, the rooms seen. Back
opens the map: every room seen, its doors, the finds in it (a dot until taken, a ring after), the shrines, the
wardens, you.

## Two players

A second pad drops in at any time (south on a pad with no seat). The camera frames both inside the room; the second
player is brought back to the first when left in another room.

## The look

Everything is drawn in code at start-up (`src/art.c`, `src/art_par.h`): a rock of its own for each region with a
normal map (weathered sandstone strata in the Lantern Gate, slate in the Archive, root-knotted rock in the Heartwood,
earth with roots and pebbles in the gardens, faceted crystal and ice, cooled basalt columns in the Forge, rock seamed
with ore in the Clockwork Deep, porous coral, billowing cloudstone), its body repeating only every two tiles each way so
neighbours always join (corners open on two sides are rounded, so rock masses read as boulders); the bright lips of
walkable surfaces, ledges, thorns and molten channels, a rough cave rock for the back wall, and six parallax families: ragged ridges, a
colonnade of arches, trees of every size, crystal spires, machinery (gears, pipes, stacks) and growth hanging from
above, each feature placed by hash so no two are alike (the trees with bark, forking branches and crowns of leaf
clusters; the arches of coursed masonry, chipped, with ivy over their tops).

**Creatures.** The hero, every enemy and every warden are skeletons of painted parts (`src/creatures.c`), posed every
frame from the simulation. The parts are painted in code at start-up by a small painter (`src/paint.c`): signed
distance shapes, each shaded as a rounded form (a value ramp from a cool edge to a lit middle, a soft key light from
above), carrying a material (fur, chitin, scales, warts, veins, feathers, brass, cloth, glass, bark, skin, moss,
membrane, stone), casting a soft contact shadow on what is under it, with a normal map from its height and a material
map (emission for eyes, glands and cores; specular for chitin, glass and brass). Painted at 128 pixels a tile (4K draws
a tile at 120) and mipmapped; each part is painted for both facings, since the lights do not turn a mirrored sprite's
normals. Every creature has a rim of its region's light along its top edges (so it reads against the dark rock), a
tint of its region, and a growth of the region's material on the backs of the crawler and the hopper.

| | |
|---|---|
| the hero | a hooded lamplighter: long coat and cape in the player's colour, a deep hood with two bright eyes, scarf, satchel, boots, a lamplighter's pole with the lantern swinging on its hook (steady through the run; dimmer as a strike sweeps it) |
| crawler | an ember-seamed husk beetle: five armour plates over a furnace-hot body; it rears, the seams brightening, and charges with its antennae swept back |
| spitter | a lantern pod: a rooted gourd with glowing veins and a sac inside; it leans away and swells, the lip-petals peeling back, then recoils as it spits |
| moth | a dusk moth: a furred body, patterned wings whose eyespots brighten as it holds them up before the swoop |
| hopper | a lamp toad: warty, with amber eyes; its throat glows as it crouches, and it leaps with its legs thrown out behind |
| knight | a hollow sentinel: old city-watch armour walked by a wisp behind its visor, a tower shield with the lantern emblem, a spear with a pennant |
| puff | a thistle puff: a ball of tinted down round a dark seed; the thorns push out through the down as it swells |
| dropper | a drip bulb: a glassy sac of glowing sap on a silk thread, a ring of little legs at its top |
| burrower | a heave of earth and pebbles; out, a head of stone plates and teeth |
| eye | a lantern eye: a great glassy eye on a fleshy stalk between bark lids |
| the wardens | a hunched beast of bark and moss with root antlers and glowing fungi; a mantis of glass with lit scythe edges; a clockwork giant with a furnace grille and turning gears; a frilled eel with glowing lateral lights; a great pale bird with a lantern crest and tail streamers; a heart-shaped seed of flesh in writhing thorn vines |

Every tell is the attack's anticipation exaggerated (the rear, the swell, the raised spear, the scythes overhead, the
coil) with a glow that grows as the attack nears, never a longer wait. A warden's stagger at half health slumps it, eased
in and out, with motes circling over it; its second phase leans its rim and tint toward its own glow and sends embers of
it up, steady. A death: the parts fly apart, turn and fall as they burn out to embers. `tests/lanternfall-creatures.mjs`
measures each creature's own pixels at game scale (from the same frame drawn as flat silhouettes): 120 colours or more
and 4 levels of fine detail (2.5 while hurt); the old flat shapes must fail.

**Scenery.** Features are set into the rock's body (`src/scenery.c`): embedded boulders, crack networks, lenses of
strata, fossil shells, pebbles, moss, roots, crystals, glowing ore, old gears, shells and barnacles, cloud wisps, each
material its own set, placed by hash two tiles deep in the rock or more, so no stretch of rock is one repeating pattern.
The props are painted too: the lamplighter statues on their plinths, garden trees, crystal clusters, machines with a
turning gear and a pumping piston, shelves of books, boulders, mushrooms, ferns, grass and flowers (these sway in the
wind). Every one-way ledge shows what holds it: a bracket from a wall beside it, chains from the ceiling, else roots.

**Rock.** The rock is drawn as one mass: each tile corner is shaded by how deep in the rock it sits and by a slow grain
across the world (no tile grid shows), and in the warm regions the deep rock leans toward the cool accent. Under every
overhang the outline is shaped past the tiles, in drawing only (the tiles stay what bodies collide with): a wedge runs
from each step's tip to the next step's tip, so a hanging mass, a tapered island or a wall bulge has one sloping face
where the tiles make a staircase, and a concave corner left over gets a round fillet to match the rounded convex ones.

**Drops.** A hole through a room's floor falls away into the dark: a dusk in the region's complementary accent under
the bridge, deepening to near black at the room's edge, with mist drifting low in it and roots, icicles, chains, weed
or wisps (by region) hanging from the bridge and the floor's edges. A hole through a ceiling darkens up into its
shaft. The drop has its own unlit layer, so its colour is what is set.

**Value structure.** Every region keeps the same steps of luma whatever its palette (`toluma` in view.c): a bright
hazy depth, three parallax depths each a step darker toward the viewer (far ridges, the region's family, a nearer
one), a back wall only in a band round the rock, the rock's body dark, the walkable lip the brightest thing in the
ground, the actors saturated. Rooms read as dark shapes against a bright distance. Framing silhouettes (columns,
trunks, chains, shards, weed, hanging roots) stand nearer than the playfield at a room's walls and ceiling and slide
past faster than the world.

**Colour.** Each region keeps its key hue and has a complementary accent: cool steel and blue pilot lights against
the warm regions (the Forge's machinery is steel blue, the far depth goes blue, glowing growth glows blue), warm
lantern posts and an amber far depth in the cool ones (Glasswater, the Tidal Halls, the Frostvault, Starfall,
the Skyreach, the Lantern Gate, the Archive, the Fen).

`tests/lanternfall-look.mjs` measures frames of eight regions: mean luma 100 to 120 (the darker Tidal Halls and Starfall
from 80), luma standard deviation 55 or more, a complementary accent in 3% or more of the saturated pixels, a floor
of local contrast; frames flattened halfway to their mean must fail.

Lamps, the lantern, shots and pickups are 2D lights on the lit layers; glows and particles go on an additive layer
over them.

**Nothing strobes.** No wash of colour ever covers the screen; a strike's sweep is a soft ribbon, the dash a faint smear; a struck creature takes a pale tint that eases off over
its flash ticks, a hurt hero a soft shimmer that swells and fades about twice a second, a tell's eye one swell per
tell, the marks of a warden's rain a slow swell; impact stars are small and fade. `tests/lanternfall-flash.mjs` draws a
warden fight frame by frame and follows every ninth of the screen through time against the photosensitive flash
thresholds (general and red): no block may flash at all, none past three a second; a quarter of the screen blinking at
5 Hz must fail. `tests/lanternfall-flashes.mjs` follows every creature closer: a second of every enemy and every warden
struck every ten ticks with their tells and attacks running, and both heroes hurt, striking and mending, in cells of 48
pixels: none may flash more than three times a second; the same scenes drawn with full white hit flashes and the old
blink must fail.

## The bot

`src/bot.c` plays through the input struct the pad fills: it stands on each route step's tile and plays its move tick
by tick; when knocked off it searches nearby with the same moves for a way back onto the route (never past a goal it
has not reached); it clears enemies first (gets under the edge of a ledge to strike up at what sits on it, steps off
the spot a moth's wings flared at and strikes it as it comes past), cuts thrown things, mends when hurt, rests at
shrines, opens hidden and cracked floors the route drops through, and reads the wardens' tells in their arenas
(charges jumped toward and over, tells as long as the warden's phase makes them, helpers struck first). A watchdog
stops it minding enemies for a while when the route has not moved in twenty seconds. It plays with the assist
(every third hit costs a leaf) and clears the game: `tests/lanternfall-soak.mjs` requires every pickup, all six
wardens and the heart down, for one player and for two, each on two runs (the sim has no randomness, so the second
run starts the bot 13 ticks late: every enemy and warden meets it at another point of its cycle). Twenty-four such runs
(twelve starts, one and two players) all clear.

## The 2D renderer's new features (cartwheel-engine v0.37)

Built against an engine whose `render2d.h` has the HDR post chain (cartwheel-engine v0.37.0), the game turns on
(`LANTERNFALL_R2D_V2`, set by CMake when the header has it): an HDR pass with bloom, a soft
tone curve that leaves ordinary art alone, a grade toward each region's accent and a vignette; emission and
intensity on everything that glows (lamp windows, braziers, crystals, the lantern, eyes, pickups, shots), so it stays
bright in a dim room and blooms; soft shadows from the rock (one occluder per run of solid tiles in a row, a distance
field in the engine) with the light reaching a little way into the rock face; light shafts; bounce light (2D global illumination, its field at an eighth of the canvas: on engine v0.38 the cheaper one, 0.2 ms
under a quarter on GL and 0.6 on WebGPU for the bench frame); real
water pools drawn from the screen (refraction, the world mirrored on the surface, caustics, ripples where you splash
and swim); shockwaves in a distortion layer (a plunge landing, a warden falling, a hidden wall breaking). The sky and every
parallax depth share one unlit layer with their haze baked into their colour (no fog pass, no extra full-screen
layers); the framing silhouettes are their own unlit layer over the water. Against the pinned v0.34.3 SDK the game draws as before (additive glows, flat water, no shadows).
