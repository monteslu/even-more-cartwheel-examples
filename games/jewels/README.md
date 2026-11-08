# jewels (working title)

A calm, untimed match-three, readable from a couch. Swap two neighbouring stones to line up three or more of a
kind; they break, the stones above fall in, and if that lines up more it goes on by itself, slowly enough to
follow. The board never moves unless the player moves it: no clock, nothing falling on a timer, no game over. An
illegal swap springs back and costs nothing, the hint is free and on its own button, a board with no move left
reshuffles itself, and the score only goes up. The best score is saved.

Ten by eight cells of 120 canvas pixels, and six kinds of stone, each its own cut and silhouette as well as its own
colour, so the board reads without colour:

| Kind | Cut | Silhouette |
|---|---|---|
| ruby | princess, stepped chevrons below | a square on its point |
| topaz | trillion | a triangle, point up |
| emerald | emerald cut, three steps above and four below | a tall rectangle with cut corners |
| sapphire | pear brilliant | a teardrop, point up |
| amethyst | marquise brilliant | a navette lying across |
| diamond | round brilliant | a circle |

## Controls

Any pad, the keyboard, the mouse and every finger drive the one board, all at once.

- Pad: the d-pad or the left stick moves the cursor; the pick button picks a stone up, and with one held a
  direction swaps it that way (or pick a neighbour, or the same stone again to put it down); the hint button shows
  a move. Prompts follow the device in hand.
- Touch and mouse: tap a stone, then its neighbour, or press a stone and drag it onto its neighbour; tap the hint
  button on the panel. All ten pointer slots are read.

## How it is drawn

The game draws in its own passes in the engine's render pipeline, the same on GL and WebGPU (`src/render.c`,
`src/shaders.c`), then the engine's temporal resolve and tone curve follow:

- **The stones are ray traced per pixel through their facets.** Each cut is laid out as a cutter lays a stone out,
  facet angles and indexes (`src/cuts.c`), and is the intersection of its facet planes, a convex polyhedron, so a ray
  inside it leaves through the nearest plane it is heading out of: no mesh, no acceleration structure. Each pixel's
  ray is refracted in, then bounced: Fresnel at every face, total internal reflection past the critical angle,
  absorption along the path (thick parts deep, edges bright), and at every exit the light that leaves is looked up
  in a studio of soft boxes, strips and three hundred small hot lamps around a dark zenith where the viewer stands
  (the dark half of a cut stone's contrast pattern). The room and its boxes are baked once into a map of the sky the
  stones read with one fetch an exit, its texels naming the nearest lamp, which is then worked out exactly. Each
  kind's pavilion is cut for its stone's index and its absorption is set so it deepens where the stone is thick and
  goes pale where light leaves straight; the princess has a brilliant crown over its stepped sides, so its face breaks
  into chevrons.
- **Fire without noise.** At each exit the spectrum fans out between the directions of 400 and 700 nm light; where
  that arc crosses one of the studio's small lamps, the wavelength that lands on it is found analytically and the
  lamp is seen in that colour, with the share of the spectrum that lands on it. The idle rock of each stone (a few
  degrees on its own slow rhythm) sweeps facets across the lamps: the scintillation of a real stone.
- **Glints** come only where a facet flashes a hot lamp: the traced picture is searched for its brightest points,
  one at most in each patch of the board and none beside a brighter one, and a star is drawn exactly where the
  highlight is, at full resolution: four fine spokes and four short ones in the highlight's own colour, longer for a
  brighter flash. As a stone rocks its facets sweep across the lamps, so a star flares and fades in a fraction of a
  second and a handful are lit across the board at a time.
- **The tray**: tufted midnight velvet, a covered button where four cells meet and a soft dish pressed in for each
  stone, its pile shaded with a sheen that grows toward grazing (the dish walls and the folds glow, the flats stay
  dark) and broad patches where the pile lies another way; each stone's shadow, and the coloured light the stone lets
  through pooled in it (traced on the CPU at load through the same facets, coloured by wavelength). The frames are
  lacquered walnut (wandering growth rings, latewood and streaks, open pores, the room in the lacquer where the edge
  turns) with a hairline of gold inlaid and a gold bead; the panel is the same velvet with plaques in fine gold rims.
- **The light of a clear.** Each stone that breaks gives off a light of its own colour: it gathers while the stone
  swells, peaks just after it breaks and fades over a few hundred milliseconds, and it lights the scene. The velvet
  round it takes the colour on its pile (the tufting and the dishes show in it), the gold and the lacquer catch it, and
  the stones beside it see it as a lamp in the room their facets are traced against: the facets that face it flash
  in its colour, and its small hot core, split by dispersion like the studio's lamps, throws fire in them. The shards
  catch it too. Where the stone stood an afterglow of its colour lies on the velvet for a moment. The lights of the
  moment are added up at a quarter of the canvas for the velvet; each stone takes the two that reach it most.
- **The effects** scale with the clear: cells, run length, crossings (an L, a T) and the step of a cascade set each
  clear's power, and power sets the lights, the shards (fragments of the stone, traced like it, each with a short
  trail), the sparks and a dust of sparkles that lingers, a ring across the velvet that bends the picture as it
  passes (a shockwave), rays of light from the bigger clears, the pad's rumble and the size of the score. A cascade
  builds a counter, links its groups with streaks of light, and warms and brightens the room step by step; four steps
  or more end in a climax: a slow swell of warm light through every stone, a sweep of brilliance across them, a
  shower round the frame and the pad's longest rumble, and the finale's word on the panel. Nothing strobes: every light
  swells and fades smoothly and sparkles twinkle slowly (`tests/jewels-flash.mjs` holds the worst case to the
  photosensitivity thresholds). Everything fades within about a second.
- **Quality**: High traces one ray a pixel with five bounces and resolves over frames; Ultra four rays and eight
  bounces; Medium and Low fewer bounces; Very Low three bounces, no fire and no glints.

## Tests

| Test | Proves |
|---|---|
| `tests/jewels-rules.mjs` | the C game plays exactly as the original Lua game: its own `main.lua` and `board.lua` driven through their pad input (`tests/jewels-rules-driver.lua`), every step of three traces compared, and every deadlocked board of a family reshuffled the same; controls break the cascade's pay, the refills and the hint |
| `tests/jewels-soak.mjs` | an hour of play a seed through every input path, every tick checked (the score never falls, an idle board has a move and no match, every move comes to rest); control: a broken cell |
| `tests/jewels-gates.json` | a game on the pad and a game by touch and the mouse (`tests/jewels-inputs.mjs` plans them), and the bot, the same on every host, GL and WebGPU; the panel's layout with every element showing (no two text boxes overlap); each with a control that must fail |
| `tests/jewels-effects.mjs` | the effects grow with the clear and with each cascade step, in the logged state and in the frames, and a clear's light lifts the stones and the velvet beside it; controls: flat effects, no light from the clears |
| `tests/jewels-flash.mjs` | nothing strobes: every tick of the worst case (a cascade of six and its climax) measured against the general and red flash thresholds of WCAG 2.3.1, by screen region and by pixel; control: a light switched on and off ten times a second |
| `tests/jewels-look.mjs` | a dark board and light stones in every cell, mid-key, never washed out, the stones in colour; control: the frames flattened |
| `tests/jewels-perf.mjs` | the worst case (long cascades of big clears) within its GPU budget at 1080p High; controls: 4K, an impossible bound |
