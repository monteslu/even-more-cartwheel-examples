# Detexto

Famous buildings of the world, brought down in one bit. Detexto is the destruction game of
[Destructo](../destructo/) (the same landmarks, launcher, ammunition, physics and modes, for one to four players
taking turns) drawn in two colours: every pixel of the picture is black or white, a fine dither of blue noise at the
screen's full resolution, with the landmark bright against its ground and every silhouette outlined. Settings has
the look (**Look**, saved): **Dither** (black and white, the default), **Dither warm** (warm white on near black),
**Dither grid** (an ordered cross-hatch) and **ASCII**, the original look: every frame as glowing characters on
black, each landmark a tower of characters in its material's colour.

## Playing

| Control | Does |
|---|---|
| left stick or d-pad | turn and raise the launcher (a small push is slow and fine, a full push quick; a tap of the d-pad is a hair of a step, a hold picks up speed) |
| right stick | move the launcher round the structure on its circle, so any face can be attacked |
| south | fire, at the ammunition's own fixed power (no charging, no power control) |
| west / east | the next / the previous ammunition: shot, heavy ball, charge |
| north | the wide view, on and off |
| start | pause (the layout is on the pause screen) |

In co-op and free fire the aim opens again the moment the shot has struck, while the rubble is still settling: the launcher turns, the ammunition changes and South fires the next shot at once (a South press while a shot is still in the air is kept and fires the moment the aim opens). On the select screen a held direction repeats, quicker the longer it is held, and the choice wraps round.

The game reads only the sticks, the d-pad and the face buttons: shoulders and triggers do nothing. With a mouse or a finger: drag to aim, tap to fire, tap an ammunition icon to pick it. Hints for a control show until it has been used once.

On the result card, west watches the last shot again and any button skips the replay. On the select screen north
turns on free fire: every structure open, ammunition without end, nothing judged.

### Results and stars

A level is judged once its world is still (at most two bodies awake for a third of a second; a turn's settle ends
after 15 s whatever still moves), never while the structure is falling. The share of the structure brought down is
counted by volume over every block the level placed (crystals not counted): a block is down when it is broken, gone
off the field, or more than its own size (and at least 0.6 m) from where it settled before play. Each landmark has a mark of its own, from
a hundredth of the Pisa tower or the Pyramids to over a third of the tall slender towers. The HUD shows the share against the mark while you play.

| Stars | Needs |
|---|---|
| one | every crystal down |
| two | ... within par |
| three | ... and the structure brought down to its mark |

The card says BROUGHT DOWN when the mark is reached and CRYSTALS DOWN when the crystals fell with the structure
mostly standing. It slides in at the right edge, clear of the pile.

### Instant replay

The simulation is deterministic, so the replay needs no saved world: a second game is made the way the level was and
stepped with the inputs the players gave (only the ticks something was held are logged). While the players aim it
catches up a few milliseconds a frame, while the live world is quiet; while a shot flies it waits on the tick before
that shot was fired. When the level is judged it therefore stands where the last shot started. The replay follows the
shot, turns slowly round the impact in slow motion for the first second and a quarter after it, and ends on exactly
the state the live game was judged on (every block's pose to the bit, the counters and the scores).

The launcher stands on a circle round the structure and rolls along it (a full turn in about nine seconds, held); its
turn and raise stay as they were, so it still points at the structure's middle from its new place, and the camera, the
aim path and the bots all work from wherever it stands. When a crystal is hidden from where the launcher stands, the
HUD says so and shows the control that moves round.

The aim path shows the shot's flight as the world will fly it, as far as the structure's near face (seen from where the launcher stands): how high it
arrives there shows, where it comes down behind does not. A ball crushes the masonry round where it first strikes (a
shot within about 1.6 m, a heavy ball 2.2 m) and drives what stands behind it back (3.4 m and 4.6 m): a shot breaks a
hole a few bricks across and knocks a small tower's top off, a heavy ball topples a tower. Charges go off on the first
thing they touch and push and break everything within about four and a half metres.


## The 1-bit looks (src/dither.c)

Every pixel of the 3D picture is ink or paper at the output's full size: black and white, or warm white
(255 229 189) on near black (9 8 6) for Dither warm. The scene renders at 100% (no scaler, no lower resolution), and the
engine's one-bit dither (`cartwheel/look.h`, `CW_LOOK_DITHER_ORDERED`) draws it at the DISPLAY stage: last in the post
chain (after the tone map, the grade and the lens), on the finished picture, one dither pixel a screen pixel
(`output_pixels`), so nothing after it adds a third colour. The game sets its tone options:

- **The tone.** Each pixel starts from the finished picture: the grey world by its luminance (`grey_gain`,
  `DITHER_WORLD` 0.8; its highlights, a blast's white core, kept), the landmark's hues by their value (`hue_value`,
  `DITHER_BLOCK` 1.15), so a block reads bright whatever its hue. A light unsharp mask (radius 2 px, `DITHER_SHARPEN`
  0.9) crisps edges, the depth draws outlines on the near side of every silhouette and crease (where the depth stops
  being a plane: `DITHER_OUTLINE`), then gamma (`DITHER_GAMMA` 1.3), contrast (`DITHER_CONTRAST` 1.3) and
  `DITHER_BIAS`. The desert levels (the sun low behind pale sand) take the world at 0.62 of that and the landmark at
  1.45 (`DITHER_DESERT_WORLD`, `DITHER_DESERT_BLOCK`), so a landmark lit from behind still reads brighter than its sand.
- **The threshold**, anchored to the screen's pixels, so a still camera gives a still picture: the engine's 64x64
  blue noise (Dither, Dither warm) or the 8x8 ordered matrix (Dither grid).

The HUD and the game's own screens draw after it in the same two colours: every HUD colour goes to the ink or the
paper by its brightness, opaque (a colour under half alpha is not drawn), the HUD's boxes and plates are square
paper with a thin ink edge on whole pixels, text has a thin outline of the other colour instead of a soft glow, and a
selected tile (the structure select, the ammunition bar) is inverted: an ink plate with paper text. The engine's menus
(loading, title, pause, Settings, the Graphics page) are in the look's two colours too (`cw_shell_desc.two_colour`
with the look's ink and paper): no dimming, each line of text on a paper plate over the picture, one-bit text edges,
the selected row an ink plate. Changing the Look in Settings changes the menus on the next frame
(`cw_shell_set_colours`, `cw_shell_set_two_colour`); under ASCII the menus are the engine's own with the selected
row on a white plate (`CW_SHELL_SELECT_INVERSE`). Every screen of the game's own (the HUD, the result card, the
structure select, the pause screen's controls card) lays its lines out by their measured size, so at 200% text
nothing overlaps and the boxes grow round it. Dust, smoke and the collapse
plume draw at half their opacity under a 1-bit look (`DITHER_CLOUDS`), so a structure reads through a cloud rather
than behind a field of dots.

Run options: `LOOK` (dither, warm, grid, ascii) picks the look as the Settings row does; `DITHER_OFF 1` shows the
plain picture (a gate's control); `MENUS_TWO_OFF 1` leaves the engine's menus in their own colours under a 1-bit
look (the menus gate's control). The engine's test switch `CW_TEST_LOOK_CRAWL=1` moves the threshold every frame
(the no-crawl gate's control).

## The ASCII look (src/ascii.c)

The 3D scene renders as Destructo's does (three.c, clustered lights, shadows), but nobody sees it: it is only what
the characters are chosen from. Two passes on the engine's render pipeline, the same shader in GLSL and WGSL:

- **The grid (POST, at the scene's render size).** The output is divided into character cells: 54 rows (20 pixels
  at 1080p), as wide as Fira Mono's advance (half the height). Each cell samples the scene in six regions (three
  rows by two columns) for its brightness and, from the depth, the near side of an edge. A cell whose six regions
  are about the same takes a character from a brightness ramp (`` .:-=+*#%@``); one with a shape to it takes the
  character whose own six-region coverage (measured from the font when the cart starts) is nearest, after a
  contrast curve, from a set of line and mark characters (`| / \ _ - [ ] ( ) < > ^ v L J T 7` and others).
  A cell keeps last frame's character unless a new one fits clearly better (35%), so the text does not shimmer
  while the camera moves. Its colour is the scene's own hue pushed to full saturation; a desaturated cell (the
  ground, trees, the ring of land far out) is drawn dim and green. The same pass blurs the bright cells' colours
  over about two cells and writes that as the picture: the glow under each character.
- **The characters (OVERLAY, at the output's full size).** Every pixel reads its cell's character from an atlas of
  the font rasterized at the cell's exact size (a pixel bolder) and adds it in the cell's colour over the glow. The
  characters never pass through a scaler, so they stay crisp at any output size.
- **The HUD on black.** Every HUD element records its rectangle while it draws; touching rectangles merge into
  boxes, which get a 70% black plate and a neon edge, and the grid pass draws the cells under them at half brightness, so a little of the scene shows through and the text stays clear. The shell's
  screens (title, settings, pause) get boxes behind their menu column and prompt line.

The world round the structure is dimmed and greyed at load (its materials), and each block takes its material's
neon hue with its own brightness kept (relief, damage, weathering), so the landmark is what glows.

Run options (`dm set NAME VALUE` on the debug channel, or the environment natively) tune the look while it runs:
`ASCII_ROWS`, `ASCII_CONTRAST`, `ASCII_EDGE`, `ASCII_KEEP`, `ASCII_FLAT`, `ASCII_GAIN`, `ASCII_GLOW`,
`ASCII_WORLD_BRIGHT`; `ASCII_OFF 1` shows the plain 3D picture (the gates' control) and `PLAIN_COLOURS 1` the
original materials.

## Performance

The scene renders at 35% of the output (`cw_quality_scene_scale`, cartwheel-engine 0.48; the run option
`SCENE_SCALE` changes it): each character region still has about two scene pixels, and the characters themselves
draw at the output's full size in the OVERLAY phase, so they stay crisp. On a Radeon 890M at 1080p (WebGPU, GPU per
frame, interleaved rounds with an A/A pair): the scene at 100% 4.42 ms, at 35% 1.33 ms (A/A 1.34), at 25% 1.01 ms.

## Tests

`tests/detexto-look.mjs` (romdev, GL and WebGPU): the frame is characters on black (over 55% black, 2 to 30% lit),
the scene renders at 35% while the cells are sized from the output, the grid covers the frame, and at most 6% of the lit characters change between two frames while the camera moves.
Controls that must fail: the pass off (the plain picture) and the hold off (`ASCII_KEEP 1`, more change).

`tests/detexto-dither.mjs` (romdev, GL and WebGPU): each 1-bit look draws only its ink and paper (at least 99.5% of
the pixels without the HUD; control: the pass off), Dither and Dither grid draw different patterns and Dither warm
the Dither pattern in its own pair, a still camera changes at most 0.5% of the pixels a frame (control: the
threshold moved every frame, `CW_TEST_LOOK_CRAWL=1`, at least ten times as many), and the Settings row steps the looks
from Dither to ASCII and stops at the ends, ASCII putting the scene back at 35%; the shell's screens under each 1-bit look show the selected row as an ink plate, and their share of ink and paper is reported. `tests/detexto-look-compare.mjs` shoots the same scenes on this build and another (`DETEXTO_OLD_CARTS`) and counts the pixels that differ. `tests/detexto-dither-shots.mjs`,
`-clip.mjs` (`--flash` for the flash check of real play per look) and `-timing.mjs` make the stills, clips and GPU
times the looks were judged by. The
simulation, levels and physics are Destructo's and are gated there (`tests/destructo-*.mjs`).
