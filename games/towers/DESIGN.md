# towers: design

A tower defense game for one to four players on one machine. Every player has a cursor and builds in their own share
of the map; one shared wave at a time comes down the paths for the core, and late waves overlap into thousands of
creeps. The towers are the show: chain lightning, freeze fields, beams and mortars, three levels each. 16:9, the
whole map on one screen, a fixed camera per map.

## The view

- The camera is fixed for the map: the whole of it (36 x 20 tiles of 2 m) on one screen, tilted 15 degrees from
  straight down toward the south through a long lens (a 14 degree field of view, close to orthographic), with a little
  room over the top for the HUD's line. About 25 pixels to a metre at 1080p.
- The path is low; the ground round it is raised half a metre with banks down to the path; rock outcrops rise out of the
  ground as faceted blocks and pools sit in bowls with wet banks. Build pads stand on the ground beside the paths; the core
  stands on a 2 x 2 block of path; the gates sit where the paths leave the map's edge.
- Creeps lean 15 degrees back toward the camera from their feet (crawler's rule: they are seen down their own up axis,
  as from straight above); towers, pads and the scenery do not lean.
- A creep is 20 to 40 pixels across, so each kind reads by its colour and its size, not its edge alone: the modelled
  body (crawler's dark body with one accent) is lifted and tinted in the kind's colour (cyan skitters, bone ghouls,
  orange hounds, gold imps, violet sporelings, lime bloaters, pale shades, crimson ogres), sizes run from the small
  skitters to bloaters half again as wide and ogres two thirds bigger, and an edge in the same hue (crawler's hull)
  parts each body from its neighbours; blob shadows stay light so a crowd is not a field of dark circles. Each tower kind has its own hue on its body
  (the head deeper than the base), its rim light and its glow, far apart round the wheel: bolt violet, frost ice-cyan,
  beam rose, mortar amber; the owner's colour is on the base's trim and a lit disc under it.
- Text is at least 2.4% of the screen's height; prompts are the engine's button prompts.

## The look: colour, not darkness

- Six themes, five maps each, each a value hierarchy with a complementary accent (mapbuild.c PAL):
  meadow (deep green ground, a path of pale packed earth in a red brick curb, blue pools), autumn woods (gold and
  red ground, blue-grey slate cobbles), desert (bright sand, red-brown sandstone flags, teal pools, red rock), snowfields (white ground, dark frozen
  earth and gravel), volcano (dark basalt glowing orange in patches, copper-green mineral crust between them and on the outcrops, a pale
  blue-grey ash crust path, lava pools of black crust split by molten cracks), crystal vale (deep violet ground, pale
  flagstones, glowing teal pools). Every path is a ground material with texture at the play camera, and its wide
  stretches (a field the creeps spread over) are paved as a plaza of square-cut slabs.
- The value hierarchy: the path a clear step lighter or darker than the ground on every theme, its edge drawn by a
  dark curb; every edge between two materials (the floor and its curb, the plaza, the outcrops' feet, the pools'
  water line) is a smooth curve: the terrain is built from the routes' own curves (map.h, the ground's shape) and
  each triangle cut along them, so no tile staircase shows anywhere; the outcrops faceted rock (below), the pools water
  with a shoreline (below); the decor thinned and darkened, cooled against a warm theme, so it frames the play instead of competing;
  the towers lifted off it with a rim light in their kind's hue, glossier surfaces, a lit disc of the owner's colour under each and
  a brighter team trim; the creeps tinted by kind with an ink outline, so they read on a pale path and on a dark
  ground alike.
- The grade (gen-luts.mjs): a strong S-curve (more on the low-contrast themes), split toning with cool shadows, the
  blacks kept deep, saturation held level; exposure per theme for a mid-key frame. tests/towers-look.mjs holds one
  map a theme to it: mean luma 90 to 135 (75 for the darker volcano and crystal), a standard deviation of 42 or
  more (the paths are their materials' colours now, not near-white fills), local contrast (luma against its 33 x 33 neighbourhood) of 13 or more, a complementary accent (4% of the
  saturated pixels 90 degrees or more round the wheel from the commonest hue), saturation 0.25 to 0.62, and no staircase edges (STAIRS_MAX: the share of
  strong edges lying along the screen's axes, at a scale past the textures' grain); frames flattened halfway to
  their mean must fail, and so must a frame with a tile staircase drawn across it.
- The outcrops read as rock: each is a block with a polygon's outline (fracture planes, never a circle), cliffs at its
  edge, a roof of tilted planes broken into strata (ledges and risers), and a few loose boulders at its foot. The rock
  is faceted: every small triangle of it is flat and lit by the sun as it faces, so its planes and creases stay sharp
  at the play camera; caps of the theme lie on its flat tops (moss, lichen, sand, snow, ash, glowing lichen), and its
  value steps away from the ground (pale on the dark meadow, dark on the snow, red on the sand).
- The pools read as water: an organic shoreline (lobes and a little noise round a stretched bowl), the surface dark in
  the deep and greener over the shallows with no pale rim, the sky's tint in soft streaks over it, a web of light in
  its texture, the bank down to it wet and dark. Glints of the sky come and go on it, drifting with the wind (view.c).
  On the volcano the pools are lava: black crust in plates, molten light in the cracks, swelling bubbles of light and
  the air shimmering over them; in the crystal vale they glow teal in facets, never a white disc.
- The ground and path are baked (the sun, the sky, the lamps and glowing decor, with the trees' and rocks' shadows)
  and drawn unlit; everything that moves takes the bake's light where it stands; the towers' flashes, the beams' hits,
  the core and the gates are clustered lights, and every moving light shows on the baked ground as a pool.
- Atmosphere: motes drifting over the map in the theme's colour (pollen, sparks of leaf, sand, snow, embers,
  glow-spores), glows under the towers and the core, light columns over the gates, glints on the water, heat shimmer
  over the lava.

## Players and controls

| | Pad | Keyboard (player 1) |
|---|---|---|
| hop the cursor to the next pad that way | d-pad or left stick | W A S D or the arrows |
| on an empty pad: build (the diamond) | north bolt, west frost, east beam, south mortar | I, J, L, K |
| on a tower of yours: upgrade (to the third level: the A branch) | south | K or Space |
| on a tower of yours at the second level: the third level's B branch | east | L |
| on a tower of yours: change its aim (first, strongest, last) | north | I |
| on a tower of yours: sell (hold) | west | J |
| call the next wave early (gold for the time left) | either shoulder | Q |
| the storm: lightning down round the cursor (every 30 s) | either trigger | E |
| pause | Start | Escape |

- The build diamond is drawn round the cursor where the buttons sit: the tower each face button builds, with its
  price, greyed when the gold is short. On a tower the same places show upgrade (with its price; at the second level
  the two branches, south and east, each with what it does), aim and a held sell.
- With two or more players the labels and the reach rings show while a player is choosing: for three seconds after
  the cursor hops or the tower under it changes, and while a sell is held; then they fade and one short line stays
  over the cursor (P1 BOLT III STORM). One player alone keeps the full labels.
- The cursor hops only between the pads of its player's share and the towers they own (the d-pad works everywhere;
  the stick picks one of eight ways). A press hops once; holding hops again after 0.3 s, then every 0.15 s.
- Shares: one player owns every pad; two split the map into a left and a right band, three into three bands, four
  into quarters, each balanced by how much path its pads reach (the map check holds them within 30%). A player who
  joins mid-map takes a share of the empty pads; towers stay with whoever built them.
- The storm is something to do during the wave, for every player: lightning comes down on every creep within 5.5 m
  of the cursor's pad (60 a creep, 9 more each wave, through no armour), stunning all but bosses for half a second;
  then it charges for 30 seconds (a bar under the player's panel, "storm" with its button when ready). Its kills are
  the player's. The bots use it on fifteen creeps or a boss. Its strikes and its light swell in over a quarter of a
  second and die away over more than a second: a slow surge of light, never a crack, so four storms called at once stay
  at two flashes a second at most in any part of the screen (tests/towers-flash.mjs, the storm scene). The coils'
  lightning swells the same way, quicker (in over a tenth of a second, out over half of one), so a coil firing twice a
  second breathes rather than blinks; the volcano as the bots play it stays at two a second too.
- Players join at any time with the join button on a free pad (the engine's shell).

## Towers

| | Bolt | Frost | Beam | Mortar |
|---|---|---|---|---|
| does | chain lightning: strikes the creep it aims at, then leaps on to the nearest not yet struck | a pulse of cold over everything in reach: slows, chips, and at level 3 every third pulse freezes | a beam held on one creep that heats up the longer it stays on it | a lobbed shell that lands where the creep will be and bursts |
| price (build, II, III) | 100, 130, 210 | 80, 110, 170 | 120, 160, 250 | 150, 190, 290 |
| reach (m) | 7.5, 8, 8.5 | 5.5, 6, 6.5 | 8, 8.5, 9 | 10, 11, 12 (not under 2.5) |
| damage | 14, 26, 44 a strike, 14% less each leap | 2, 5, 10 a pulse | 26, 48, 84 a second, up to three times that after 3 s | 46, 82, 140, half at the blast's edge |
| period | 0.83, 0.73, 0.63 s | 1.33, 1.2, 1.07 s | held | 2.5, 2.27, 2 s |
| extra | 3, 5, 8 leaps of 3.6 m | slows 40%, 50%, 60% for 2 to 2.5 s | level 3 pierces: everything on its line to its reach takes half | blast 2.2, 2.6, 3 m; level 3 throws four bomblets round the blast |

The third level comes in one of two branches, chosen as it is bought (A south, B east); A is the third level as it
always was, so the map tuning holds, and B trades it for something else:

| | A | B |
|---|---|---|
| bolt | STORM: eight leaps | ARC: three leaps of 52, each creep struck stands stunned for a third of a second (not bosses, not shades) |
| frost | SHATTER: every third pulse freezes | BLIZZARD: reach 9 m, slows 68% for 2.8 s, 7 a pulse, never freezes |
| beam | LANCE: pierces the line | PRISM: 75% on its creep, and splits on to the two others nearest it (within 3.5 m) at 50% each |
| mortar | CLUSTER: four bomblets | NAPALM: no bomblets; the path burns where it lands (2.6 m, 3 s, 26 a second through armour) |

The towers work together, so a mixed defence beats four of one kind: a mortar's blast or a napalm fire hits a frozen
or stunned creep half again as hard (it shatters), a beam heats twice as fast on a slowed creep, and lightning leaps
4.8 m instead of 3.6 off a chilled one.

Selling returns 70% of what a tower cost. Armour (imps, ogres) comes off every hit but the beam's. Shades take half
the cold and never freeze, and take half again from lightning.

The effects (fx.c, view.c):
- Bolt: jagged lightning from the coil's orb, hop by hop: one fork a strike that writhes smoothly from one jagged
  shape into another as it fades over 16 frames, a white core and a wide blue glow, a light swelling at the orb and
  electric sparks at each strike; the coil charges up before a shot and cools after it.
- No strobing, anywhere: every light and glow swells in and fades out (lights over 4 frames in, and out over 14 or
  more), nothing blinks or flickers (a beam's shimmer is under one cycle a second, a hit warms a creep toward white
  and lets it cool), no flash is red (a leak flashes amber), and a pooled light is only ever taken back when it has
  dimmed. The explosions' own flash light starts at its peak, so it is off and the blasts swell a pooled light
  instead. tests/towers-flash.mjs holds it (below).
- Levels read at a glance: a glowing ring round the base at II; at III a second ring and a crown over the
  head, in the branch's hue (B branches glow in a second hue: arc white-cyan, blizzard white, prism gold, napalm
  red-orange). An upgrade lifts the tower and settles it over half a second.
- Kills: the body tips over onto its side and sinks into the ground over half a second, darkening (a shade fades into
  its wisp), and a coin rises off it. Each wave's banner says what each player did in the wave before it (kills and
  damage), in their colours.
- The crowd's edge takes the value opposite the path it walks (dark ink on the pale and middling paths, a pale rim of
  the kind's hue on the dark ones: the snowfields' frozen earth and the desert's red-brown flags); a boss stands in a red ring;
  tests/towers-crowd.mjs holds the crowd off its ground on every theme.
- A packed crowd reads as figures, not a carpet: each body has a size and a shade of its own and is drawn a little apart
  from the bodies it overlaps (at most 0.45 m from where the rules have it, eased; render only), and the big bodies
  (ogres, bloaters: an ogre wave is a column of them shoulder to shoulder) carry a rim of their kind's hue inside the ink
  edge, baked into their colours by how near each point is to the silhouette seen from above (no lighting model of its
  own: a rim shader on the late waves cost 0.4 to 0.6 ms of GPU). On the autumn road the bodies are shaded down a
  little so the spaced crowd stays as far below the road as the packed one was.
- Frost: a ring of cold racing out over the ground, ice motes thrown up in a ring, a cold flash; a freeze shatters
  ice shards off each creep it catches; the tower's reach glows faintly on the ground.
- Beam: a white-hot core and a magenta glow from the emitter to the creep, swelling in on a new creep and fading out
  where it was when it lets go, growing wider and hotter as it ramps; sparks spray off the creep, heat haze over it, a light rides the hit, a glow pool under it.
- Mortar: a muzzle flash and a puff of smoke, the head recoils, a shell on its arc with a burning fuse trail, and an
  Explosion3D where it lands (fire, sparks that leave smoke, a shockwave ring, debris, a cooling scorch).
- Pooled: one system of burst slots per effect kind, a pool of explosions, flash lights and rings; the quality
  presets thin the particles and the secondary lights, never a tower's effect.

## Creeps

| Creep | Health | Speed m/s | Bounty | Leak | Trait |
|---|---|---|---|---|---|
| skitter | 8 | 3.4 | 1 | 1 | the swarm: three come for every one of the others |
| ghoul | 22 | 2.4 | 2 | 1 | |
| hound | 16 | 3.9 | 2 | 1 | quick |
| imp | 28 | 2.7 | 3 | 1 | armour 3 |
| sporeling | 40 | 2.0 | 4 | 1 | heals those within 2.5 m by 5% every 0.75 s (a creep at most once in that time) |
| bloater | 75 | 1.5 | 5 | 2 | bursts into four skitters |
| shade | 24 | 3.1 | 3 | 1 | shrugs off half the cold, never freezes |
| ogre | 230 | 1.3 | 15 | 3 | armour 6; the boss is an ogre two and a half times the size (every tenth wave and the last) |

A boss has the health of 2 to 4 ogres of its wave (the last wave's 2.5 to 9) and walks 1.25 to 1.5 times as fast: the
low end on the first maps, where a boss is a lesson, rising over maps 4 to 18 to the full strength from there on.

Health climbs with the wave (diff x (1 + 0.08 k + 0.002 k^2), k the wave less one); a wave sends
30 x 1.19^k x diff^0.5 creeps (skitters three for one, ogres one for six) over 10 to 22 seconds, shared over the gates
by the length of each gate's road to the core (a gate a short walk from the core sends fewer, so every road is the same
test). The map's kinds arrive in its order, a new one every few waves; the "next" line under the wave clock names a
kind the map has not sent before with what it does and the tower that answers it ("new: imps, armoured: beams burn
through"). Creeps are agents of the engine's crowd on the map's
navmesh (the path tiles' part of the drawn floor, so no creep walks on a curb), steered by one flow field from the core; the crowd keeps them apart.

## Waves, gold and the core

- The first wave comes 24 s after the map starts; each wave starts the next one's clock (22 s and a little more each
  wave), so late waves overlap. Any player can call the next wave early: everyone gets a gold for every second left.
- Every wave pays every player (30 + 5 x wave, shared out); every kill's bounty goes into one pot shared out equally,
  so a share far down the path is never starved. Each player starts with 420 shared out (at least 110 each).
- The core holds 20; a creep that reaches it costs its leak (a boss 10). The map is held when its last wave is beaten.
  A held map leads to the next; a fallen core starts the map again. The map screen offers every map reached.

## Maps

- 30 maps (`assets/maps/<nnn>.cwtd`, format in `map.h`), 36 x 20 tiles, made by `tools/gen-maps.mjs` from a seed each.
  Paths are smooth curves through waypoints, rasterised to the tiles, mostly two tiles wide with chokes a tile and a
  half wide and wide stretches. Every layout is one of six families, in turn: switchbacks across the map, columns up
  and down it, a spiral in to the core, a loop that passes the core and circles the map before coming in from the
  other side, an open field the creeps spread over before a choke into the core, and a long wandering way. From map 7
  a second gate's path merges into the first part way along (two gates to map 18, two or three after). The generator
  rejects a layout whose runs touch (a gate's way to the core must be about as long as its route) or whose gates are
  not two tiles on the edge, or whose floor is shut by the pads' level ground. Pads go where they reach the most
  path, none touching, none pushing far into the floor. Each route is written to the map (`route` records), so the
  game draws the ground from the very curve the tiles were rasterised from.
- Terrain features (`feature` records): rock outcrops (faceted blocks run together into ridges, boulders at their
  feet) and pools with banks (a bowl whose surface is drawn with the terrain at the water's level; lava on the volcano,
  glowing pools in the crystal vale), on open ground away from the paths and pads. Each takes its shape from where it
  stands (map.c, the same in tools/fields.mjs), always inside its record's radius. The theme's decor fills the rest (big props only where nothing walks or builds round
  them), lamps line the paths.
- Waves climb from 10 (map 1) to 25 (map 30) and the creeps' kinds widen from three to all eight. The first ten maps
  keep a gentle difficulty curve; from map 11, `tools/tune-maps.mjs` bisects each map's difficulty on the soak build
  so two bots hold it but lose some of the core: none on map 11, 14 of 20 on map 30 (`tools/map-diffs.json`).
- The difficulty as a first-time player meets it (`tests/towers-newcomer.mjs`): `newcomer.c` plays a seat from what the
  screen shows at a person's pace (a second or two to decide, a hop a press, a look before it builds, a few seconds
  watching; never a wave called early, a sale or the aim changed; the storm found late). It holds maps 1 to 7 alone and
  in two on every seed, and at most half the runs of maps 16 to 30 alone; the bots of the map check hold them all.
- `tools/mapview` (`towers_mapview`) builds a map exactly as the game does and bakes its lightmap (the sun, sky, lamps,
  glows and the props' shadows); the bake records the hash of what it baked.
- `tools/check-maps.mjs` (CI) fails a broken map: every gate reaches the core, every pad beside the path and reaching
  enough of it, pads not touching, fair shares for two, three and four players, every prop on flat ground (its
  model's footprint over the ground's shape, tools/fields.mjs mirroring map.c), the features off the floor and the
  pads, a walkable floor from every gate to the core with room to pass, the bake present and matching, and
  two bots hold the map in the soak build (CI builds `towers_soak` natively for it). The maps in `tools/maps-broken/`
  (written by `tools/gen-broken.mjs`) are each broken on purpose in one way and must each fail their own check.

## Performance

- One flow field a map on a 1 m grid; the engine crowd keeps the creeps apart. No per-creep path planning.
- Creeps drawn as instanced meshes, one per animation frame and kind, vertex animation baked into `.cwchar`; their
  edges are unlit hulls sharing the bodies' instance buffers. Towers, pads, shells, beams and lightning instanced too.
- The ground and path baked and drawn unlit; moving lights clustered and pooled on the ground as splats.
- Perf gate (`tests/towers-perf.mjs`): towers at level 3 on every pad of a late map, 2400 creeps kept on its paths,
  1920 x 1080 High on the 890M, GL and WebGPU, bounded at 4.3 ms of GPU a frame (3.89 to 4.26 ms measured across six maps).

## Photosensitivity

`tests/towers-flash.mjs` captures every tick of four seconds of the perf bench (map 26, every pad at the third
level, both branches, 2400 creeps) and of the volcano at wave 18, and measures them with the engine's flash check
(WCAG 2.3.1's general and red flash thresholds over a sixth of the screen, small lights switching together, regular
flicker from 3.5 Hz). The effects alone (the crowd hidden) are held to the same bounds again on regions a 24th of
the screen, stricter than the guideline, for small effects. Its controls must fail: the old effects
(TOWERS_STROBE_CONTROL) and the engine's 8 Hz full-screen strobe over every capture.

## Feel

- No screen shake: big blasts, leaks and a boss falling rumble the pads.
- Sounds are synthesised (main.c): every tower's shot, blasts, building, upgrading, selling, a leak's alarm, the wave's
  horn, the cursor's tick.
