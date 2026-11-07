# explorer (working title): design

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
  them, the floor's steps, thorn pits and pools, platforms, then lamps, props, enemies, each placed on what holds it.

The abilities come in this order: the spark (ranged, at the start), the second jump (behind the Moss Warden), the dash
(behind the Glass Mantis), wall climbing, the plunge (behind the Brass Sentinel), the tide charm (behind the Tide
Serpent), the grapple line. The Sky Warden is the fifth beacon; the Heart of Thorns is the end.

## The proof

`tools/check.c` (`explorer_check`) walks the world with the real movement code (`src/player.c`): a node is a tile a
player can stand on, an edge is a move (`src/reach.c`: a run-up, a jump held so long, a second jump at a tick, a dash,
a climb, a plunge, a swim, a chain of swings on the grapple line) played from three spots on the tile that must all
land on the same tile. It proves, stage by stage from the start with no abilities:

- the abilities come one stage at a time, in the order above;
- every place reached at a stage can get back to the start at that stage (no soft lock);
- every pickup and every warden is reached;
- every gate room's far half cannot be reached with only the abilities that come before its own, and can with it;
- every prop, lamp, pickup, shrine, anchor and ground enemy is held by the tile it says (nothing floats).

It writes the bot's route (`assets/route.bin`): the moves from the start through every pickup, shrine and warden.
`tests/explorer-world.mjs` checks both shipped files are current, and runs five broken worlds that must each fail
for their own reason.

## Movement

Measured by `tools/feel.c` (`tests/explorer-feel.mjs`), each with a control that turns the feature off and must fail:

| | |
|---|---|
| held jump | 4.4 tiles; a tapped one 2.0 (letting go cuts the rise: gravity triples) |
| apex | lighter gravity near the top of a held jump |
| coyote time | 6 ticks: a jump still works just after running off a ledge |
| jump buffer | 8 ticks: a jump pressed just before landing jumps on landing |
| run jump | 7.1 tiles; with the second jump 10.8 |
| dash | 4.8 tiles in 10 ticks, once per airtime |
| ceiling corners | a head that clips a corner is carried round it |

Wall climbing clings when you hold toward a wall in the air, climbs on up and down, and goes over the top. The plunge
is down + dash in the air. In deep water the tide charm swims; without it the cold sends you back to the last firm
ground. The grapple line catches the nearest anchor overhead, swings, reels on up and down, and lets go with a jump.

## Combat

A strike (west) in front, up or down (in the air; a downward strike bounces off what it hits, thorns too), and cuts
small thrown things out of the air. The spark (north) costs energy; strikes refill it and it creeps back on its own.
Down + spark on the ground mends a leaf of health for two energy. Enemies show their tells: the crawler rears up
before it charges, the spitter swells, the moth's wings flare before it swoops, the hopper crouches, the knight raises
its spear (and turns strikes from the front with its shield). A strike breaks off an attack in progress.

Wardens use a shared set of moves, each with a tell long enough to read (never under 26 ticks): a charge, a leap that
sends ground waves, a volley (the Moss Warden lobs spores at glowing marks), rain from marked spots, a sweep along the
floor (jump it) or at head height (stay down), a dive at a marked point, summoned helpers, eruptions at marked spots.

## Saves, travel, the map

Up beside a shrine rests there: full health, a save, and the shrine's waystone lit; the shrine's menu travels to any
lit shrine. The save keeps abilities, finds, the walls and floors broken, the wardens fallen, the rooms seen. Back
opens the map: every room seen, its doors, the finds in it (a dot until taken, a ring after), the shrines, the
wardens, you.

## Two players

A second pad drops in at any time (south on a pad with no seat). The camera frames both inside the room; the second
player is brought back to the first when left in another room.

## The look

Everything is drawn in code at start-up (`src/art.c`): tile sets with normal maps for six materials, the bright lips of
walkable surfaces, ledges, thorns, back walls, the parallax silhouettes. The hero, enemies and wardens are skeletons of
shapes animated every frame (`src/view.c`). Value steps: the parallax and back wall sit back, the rock face is mid,
the walkable top is the brightest thing in the ground, the actors are saturated. The back wall fades out away from the
rock, so big rooms open onto the deep parallax behind them. Lamps, the lantern, shots and pickups are 2D lights on
the lit layers; glows and particles go on an additive layer over them.

## The bot

`src/bot.c` plays through the input struct the pad fills: it stands on each route step's tile and plays its move tick
by tick; when knocked off it searches nearby with the same moves for a way back onto the route; it clears enemies
first, cuts thrown things, mends when hurt, rests at shrines, and reads the wardens' tells in their arenas. It plays
with the assist (every third hit costs a leaf).

## The 2D renderer's new features (cartwheel-engine v0.37)

Built against an engine whose `render2d.h` has the HDR post chain (cartwheel-engine branch engine-2d, the v0.37
work), the game turns on (`EXPLORER_R2D_V2`, set by CMake when the header has it): an HDR pass with bloom, a soft
tone curve that leaves ordinary art alone, a grade toward each region's accent and a vignette; emission and
intensity on everything that glows (lamp windows, braziers, crystals, the lantern, eyes, pickups, shots), so it stays
bright in a dim room and blooms; soft shadows from the rock (one occluder per run of solid tiles in a row, a distance
field in the engine) with the light reaching a little way into the rock face; light shafts; bounce light (2D GI); real
water pools drawn from the screen (refraction, the world mirrored on the surface, caustics, ripples where you splash
and swim); shockwaves in a distortion layer (a plunge landing, a warden falling, a hidden wall breaking); fog on the
parallax layers. Against the pinned v0.34.3 SDK the game draws as before (additive glows, flat water, no shadows).
