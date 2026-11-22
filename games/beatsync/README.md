# beatsync

A house-music rhythm game on three buttons. You are the rhythm section of an instrumental house track: play the kick,
the clap and the bass, and the song plays the way you play it. Every frame is a step sequencer drawn in neon
characters.

- **Three buttons, one thumb**: the kick, the clap and the bass on the three lower face buttons (left, bottom,
  right). Never two notes at once. In a cart the host's on-screen pad shows those three buttons on a phone; a native
  build has three touch zones across the bottom of the screen; J K L on a keyboard.
- **One or two players**: one player on the three lanes, or on two lanes (the drums: kick and clap; or the groove:
  bass and the synth, the chords and stabs of the record); two players side by side on their own pads, each on a pair on their own road
  at their own difficulty, each with their own streak, one score between them. Two lanes play on West and South, both at once,
  and carry more notes a button than the three-lane game.
- **Tempo on the shoulders**: while a song plays, L slows it and R speeds it up, 5% a press from 70% to 130%, on
  either pad, and the song keeps its pitch (a time stretch, the engine's keylock: no chipmunk voices). The BPM as
  played shows under the difficulty. The notes, the judgement and the clock all follow the tempo; the windows stay the
  same in real time, and the score counts each hit times the tempo, so a faster song is worth more. The tempo carries
  from song to song.
- **The song reacts**: each part sits a little under the record until you play it; every note you hit punches its
  part to full and opens its top. A miss drops only that part (a short dip with a low-pass) and it comes back by
  itself over a beat. Too many misses and the floor empties.
- **On the audio clock**: notes are placed and judged on the song's own position (the mixer's frames played), not on
  frames. The windows suit a person on a living-room setup: Perfect within 75 ms and Good within 150 on Easy, 55 and
  110 on Medium, 35 and 85 on Hard. A wrong button never costs the note: it breaks the combo and that note can only be
  Good (so pressing every button scores Good at best with no multiplier). A calibration screen (tap along to a click)
  saves the offset of this screen and speaker.
- **The look**: a note highway of three rivers side by side, laid out as the buttons are: kick on the left (west),
  clap in the middle (south), bass on the right (east), so which button a note wants is where its river is. Each river
  is a road of characters receding toward a horizon near the top of the screen, a current streaming down it to a gate
  near the bottom; a note rides it down, small far off and growing all the way, and reaches the gate exactly on its
  beat. A hit bursts at the gate in shards and a ring (bigger on a Perfect), blooms the gate and surges the river; a
  miss dims the river for a beat. Perfect shows green, Good turquoise, Miss red, on every river. Beside the rivers, a land of characters is the
  other track (chords, pads, stabs), a row every half beat riding down with the notes: it rises before a loud stretch
  is heard, takes the harmony's colour (a chord change is a new colour coming down), lights its ridge on stabs. The
  multiplier sits under the middle gate; the streak charges the banks from the gates to the horizon at x4, each step
  sends a band up the rivers, and a broken streak drains back down in red. Each section has its palette. Easy shows eight beats of each river, Medium 6.4, Hard
  5.2 (twice what they showed before, with the notes' last beat as fast as it was), so Easy is the slowest to read.
- **No waiting**: play starts 8 beats before the level's first note, and the charts leave no long holes; if a wait of
  more than three bars remains, the banks fill as it passes and a countdown says when the notes come back. The ASCII pass is Detexto's.
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
| `tests/beatsync-depth.mjs` | the rivers in perspective: in every lane each note is wider than the one behind it, a note at the gate at least three times as wide as at the far end, at least 1.9 times 0.31.0's beats ahead on screen, and the speed at the gate within 10% of 0.31.0's (Easy, Medium, Hard, GL and WebGPU) | every note one size (`notescale flat`); notes shrinking as they come (`notescale reverse`) |
| `tests/beatsync-mix.mjs` | from the recorded sound (GL): a kick miss dips the kick at least 6 dB and it is back within 1 dB a beat and a half later, while clap, bass and other stay within 0.5 dB; on Easy a kick the player hit stands at least 3 dB over one nobody played | a miss that ducks every lane (`missall 1`); the reward off (`reward 0`) |
| `tests/beatsync-wait.mjs` | every song and level starts within 8 beats of its first note; with a 20 s hole cut into a chart, the wait indicator is on screen in the wait and gone (the same frame as without it) two beats after the notes come back (GL and WebGPU) | every song from its top (`fromzero 1`); the indicator always up (`gap force`) |
| `tests/beatsync-flash.mjs` | every effect's flash meter reads at most 3 flashes a second over a whole song, and romdev's picture check (WCAG 2.3.1, Harding) passes over the densest songs played whole on Hard (Test Groove, the most notes a second, and Acid House Rave, the most notes) Test Groove on Easy (brighter notes), and the two-lane game on Hard on the song with the most groove notes: one player on the drums and on the groove, and two players on four rivers | every effect strobing; the picture with an 8 Hz strobe mixed in |
| `tests/beatsync-programs.mjs` | the program table has every draw state the game asks for, at every preset and in a player's first frames | a sphere in a material no table has |
| `tests/beatsync-text.mjs` | text at least the engine's size floor on every screen | the same screens at half text size |
| `tests/beatsync-bass.mjs` | in the whole mix (GL): a missed bass note is at least 8 dB under a hit one under 250 Hz, bass notes hit stand at least 2 dB over the record, and a missed bass-lane note made from the other track (a break) is at least 3 dB under a hit one from 300 Hz to 4 kHz | the bass at the kick and clap's levels (`bassmix old`); break notes acting on the bass lane (`othersrc 0`) |
| `tests/beatsync-forgiving.mjs` | a person on a living-room setup (pressing 50 ms late on average with a 50 ms spread, a slipped thumb every 15 notes) clears the first song on Easy at accuracy 0.70 or better and on Medium at 0.55 or better, from the on-screen cue (GL and WebGPU) | the same player under the old judgement (`rules old`: 35/70 ms everywhere, a wrong button misses); a masher pressing every button on every note must get no Perfect and no combo |
| `tests/beatsync-land.mjs` | every land row drawn is the song's analysis of the other track for its half beat, the land changes with the music and the picture follows it | the land off (`land off`); one flat land (`land flat`) |
| `tests/beatsync-streak.mjs` | the same deterministic run with and without the streak's picture: the banks' charge shows and reaches the far end at a streak of 50, a break drains in red | the run without the streak's picture against itself |
| `tests/beatsync-duo.mjs` | the autopilot plays Test Groove as two players (drums and groove at two difficulties, and swapped) and as one player on each pair, every note hit and each player's notes exactly their pair's chart; two pads reach two-player play through the menus; pad 2 alone hits player 2's notes and none of player 1's; every river each layout shows (three lanes, each pair, two players) is lit over its gate in the picture; player 2's miss breaks only player 2's streak | the counts held to the difficulties swapped; only one player ready; each player on the other's pad (`padswap 1`); a hidden river drawn dark where it rests (`rivers hidden draw`); one shared streak (`sharedstreak 1`) |
| `node tests/gate.mjs --gates-file beatsync-gates.json --hosts romdev` | the autopilot plays Test Groove on Hard to the results with every note Perfect, and the menus reach play from the pad, the same state on GL and WebGPU | each run cut short |

The flash sweep (`tests/flash-sweep.mjs beatsync-hard`) holds a stretch of Acid House Rave on Hard to the same rules.
The song tooling's own checks run in CI: `node --test games/beatsync/tools/` (the DSP pieces against known signals,
the chart rules and the checks' controls) and `tools/check-songs.mjs` over every package (each with a control).
