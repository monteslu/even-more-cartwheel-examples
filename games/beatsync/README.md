# beatsync

A house-music rhythm game on three buttons. You are the rhythm section of an instrumental house track: play the kick,
the clap and the bass, and the song plays the way you play it. Every frame is a step sequencer drawn in neon
characters.

- **Three buttons, one thumb**: the kick, the clap and the bass on the three lower face buttons (left, bottom,
  right). Never two notes at once. In a cart the host's on-screen pad shows those three buttons on a phone; a native
  build has three touch zones across the bottom of the screen; J K L on a keyboard.
- **The song reacts**: each part plays at full volume while you hit it; a miss ducks that part (a short fade with a
  low-pass sweep) until your next hit in it. Too many misses and the floor empties.
- **On the audio clock**: notes are placed and judged on the song's own position (the mixer's frames played), not on
  frames: Perfect within 35 ms, Good within 70. A calibration screen (tap along to a click) saves the offset of this
  screen and speaker.
- **The look**: a note highway of three rivers side by side, laid out as the buttons are: kick on the left (west),
  clap in the middle (south), bass on the right (east), so which button a note wants is where its river is. Each river
  is a current of characters streaming down to a gate near the bottom; a note rides it down, nearer and larger as it
  comes, and reaches the gate exactly on its beat, condensing from `+` to `#` to `@`. A hit blooms the gate and surges
  the river; a miss dims the river until the next hit. Each kick sends a shockwave of characters across the field,
  claps throw sparks off their gate, the bass is a waveform along the bottom, and a field of glyphs behind it all
  follows the song's energy; each section has its palette. Easy shows four beats of each river, Medium a little over
  three, Hard a little over two and a half, so Easy is the slowest to read. The ASCII pass is Detexto's.
- **Songs**: six built in (`songs/<id>/`: four sample-aligned Ogg Vorbis stems and their charts), from a short
  synthetic warm-up to Acid House Rave at 129 BPM; Easy, Medium and Hard each; scores, grades and bests saved per song
  and level. `songs/order.txt` sets the order (slowest and sparsest first).

`docs/DESIGN.md` is the contract between the song tooling and the game; `docs/TOOLS.md` is the tooling.

## Build and play

From the repository root:

```sh
scripts/fetch-deps.sh                   # once: the pinned engine SDKs
scripts/build.sh beatsync beatsync_auto # build/native/games/beatsync/beatsync and build/cart-wgpu/carts/beatsync.wasc
```

`beatsync_auto` is the same game playing itself: straight into Test Groove on Hard, every note hit as it reaches the
line, and round again. Its debug request `song <index|id> [easy|medium|hard]` plays another song.

## Making a song

The tooling in `tools/` (Node 24 ESM and WebAssembly; nothing installed on the host) turns an NI Stems `.stem.mp4`
(master, drums, bass, other, vocals) into a song package: it splits the drums into kick and clap, finds the tempo,
the beats and the bars, picks onsets per lane, writes Easy, Medium and Hard charts and the four tracks, and proves the
tracks are sample-aligned with three Vorbis decoders. From `games/beatsync/`:

```sh
npm ci --prefix tools
node tools/make-song.mjs "/path/Song Name.stem.mp4" --out songs/song-id --title "Song Name"
node tools/check-songs.mjs songs/song-id
```

then build again from the root. The stem files stay outside the repository; `songs/*/preview.wav` (the tracks with a
click on every Hard note, for listening) is ignored by git. The tooling's npm packages are development dependencies
only: `@ffmpeg/core` is GPL-2.0-or-later, and is never linked into the game or shipped in its cart, which carries only
the tracks and charts it wrote (docs/TOOLS.md, "Dependencies").

## Tests

Each runs the dual cart through romdev on GL and WebGPU (`--gpu gl|webgpu|both`, default both), inside the engine's
GPU suite lock (`node deps/cartwheel-engine/tools/with-gpu-suite.mjs <name> node tests/beatsync-<gate>.mjs`), and
each has a control that must fail:

| Gate | Passes when | Its control (must fail) |
|---|---|---|
| `tests/beatsync-sync.mjs` | a bot that reads the song clock once, then presses every note of Test Groove on Hard by the wall clock, gets every note Perfect; the recorded kick (romdev's audio record, GL) lands on the game's clock | every press 50 ms late (no Perfect); the chart moved 80 ms against the recording |
| `tests/beatsync-newcomer.mjs` | a bot that reads only the next note on the rivers, with a human's timing error, reaches the song through the menus on a pad and clears it on Easy | the same bot ignoring the cue |
| `tests/beatsync-look.mjs` | the play field is neon characters on black, drawn as strokes, the grid covering the frame | the ASCII pass off |
| `tests/beatsync-layout.mjs` | each button's press lights its own river, west's left of south's left of east's (from the picture) | the rivers swapped (`lanes 2 1 0`) |
| `tests/beatsync-flash.mjs` | every effect's flash meter reads at most 3 flashes a second over a whole song, and romdev's picture check (WCAG 2.3.1, Harding) passes over the densest songs played whole on Hard (Test Groove, the most notes a second, and Acid House Rave, the most notes) | every effect strobing; the picture with an 8 Hz strobe mixed in |
| `tests/beatsync-programs.mjs` | the program table has every draw state the game asks for, at every preset and in a player's first frames | a sphere in a material no table has |
| `tests/beatsync-text.mjs` | text at least the engine's size floor on every screen | the same screens at half text size |
| `node tests/gate.mjs --gates-file beatsync-gates.json --hosts romdev` | the autopilot plays Test Groove on Hard to the results with every note Perfect, and the menus reach play from the pad, the same state on GL and WebGPU | each run cut short |

The flash sweep (`tests/flash-sweep.mjs beatsync-hard`) holds a stretch of Acid House Rave on Hard to the same rules.
The song tooling's own checks run in CI: `node --test games/beatsync/tools/` (the DSP pieces against known signals,
the chart rules and the checks' controls) and `tools/check-songs.mjs` over every package (each with a control).
