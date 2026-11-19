# beatsync song tooling

The tools that turn a split song into a package the game plays (`docs/DESIGN.md`, "The song package" and "Charts").
Plain Node (24+) with npm packages and WebAssembly; nothing is installed on the host and no ffmpeg binary is used.

Every command below runs from `games/beatsync/`.

```sh
npm ci --prefix tools          # once: the WASM codecs and the stem-mp4 container library
```

| Command | What it does |
|---|---|
| `node tools/make-song.mjs "<file.stem.mp4>" --out songs/<id> [--title ..] [--artist ..]` | the whole pipeline: a song package |
| `node tools/check-songs.mjs [songs/<id> ...]` | the checks over every package (or the ones named), with their controls; exits 1 on a failure |
| `node tools/make-test-song.mjs [--variants]` | the synthetic house song with known notes (stems, `.stem.mp4`, `truth.json`) |
| `node tools/eval-accuracy.mjs [--source mp4\|wav]` | the analysis measured against the synthetic songs' ground truth |
| `node --test tools/` | the unit tests (the DSP pieces against known signals, the chart rules and the checks' controls) |
| `node tools/stb-vorbis/build.mjs --stb <stb_vorbis.c>` | optional: builds the engine's own Vorbis decoder to WASI so the alignment proof and the checks also decode with it |

## Adding a song

1. **Make the track**: an instrumental house track (no vocals, or the vocals are thrown away).
  
2. **Split it in Loukai**: it writes an NI Stems `.stem.mp4` (master, drums, bass, other, vocals; AAC). Keep that file
   outside the repository; it is never committed.
3. **Make the package**: `node tools/make-song.mjs "/path/Song Name.stem.mp4" --out songs/song-id --title "Song
   Name"`. About 40 s for a 3-minute song. It prints the BPM found, the sections, the notes per level and the size.
   If Loukai filed an instrumental lead under vocals (listen to the vocals stem), add `--keep-vocals` to fold it into
   the other track. A song outside 115-135 BPM needs `--bpm-min` / `--bpm-max`.
4. **Listen**: `songs/song-id/preview.wav` is the four tracks with a click on every Hard note (low click kick, high
   click clap, middle click bass). The clicks should sit on the hits all the way through. Read `report.json` for the
   numbers (tempo, onsets per lane, what was rejected, the alignment proof).
5. **Check**: `node tools/check-songs.mjs songs/song-id`.
6. **Build and commit**: `scripts/build.sh beatsync` from the repository root lists the new song in the game
   (`songs/order.txt` sets the order); commit `songs/song-id/` (`preview.wav` is ignored by git and never shipped).

## The pipeline

1. **Read**: the NI Stems container is read with [stem-mp4](https://www.npmjs.com/package/stem-mp4) (the same library
   Loukai writes with; track 0 is the master, the stems are named in the NI `stem` atom); each AAC track is decoded by
   ffmpeg compiled to WebAssembly (`@ffmpeg/core`). The AAC encoder's priming (1024 samples) stays at the start of
   every track alike, so the tracks stay aligned with each other and the charts are made from the very audio that ships.
2. **Split the drums** at 150 Hz with a zero-phase Butterworth low pass run forward and backward (the Linkwitz-Riley
   magnitude, no group delay) and its exact complement: kick + clap is the drum stem sample for sample.
3. **Onsets per lane**: log-magnitude spectral flux in the lane's band (kick 20-160 Hz, clap 1-5 kHz, bass 25-400 Hz),
   an adaptive threshold (moving median plus two mean absolute deviations, never under 30% of the loudest flux within
   1.5 s), peaks at least 50 ms apart, then each onset moved to where its attack starts on the band's envelope.
   - **Clap**: an onset whose rise is mostly above 6 kHz is a hi-hat or shaker and is dropped; a weak clap-band onset
     on a kick is the kick's beater click and is dropped.
   - **Bass**: the bass bins are averaged over about a period first (a held low note's harmonics beat at the
     fundamental and would look like onsets). Kick bleed in the bass stem is cancelled by a least-squares filter from
     the kick track. Pitch changes from YIN add legato notes, which have no attack. An attack on a kick counts only if
     it brings a new pitch or a steady pitch out of quiet.
4. **Tempo and beats**: autocorrelation of the kick (plus half the clap) onset envelope over 115-135 BPM, the
   Ellis dynamic-programming beat tracker, then each beat fitted to the kick (or clap) onsets around it by a local
   straight line over +-8 beats, which follows a drifting tempo and holds steady where a beat has no onset.
5. **Bars**: claps on 2 and 4 fix the beat's place in the bar; the other stem's changes (chords) choose between the
   two phases left.
6. **Quantize** to 16ths; an onset more than 40% of a 16th from its step is dropped.
7. **Charts**: every candidate scores its strength (saturating at the lane's median), its place in the bar, the lane
   that owns that place (clap on 2 and 4, kick on 1 and 3, bass off the beat) and its novelty (a fill beats the
   hundredth plain kick). Bars with alike onsets share a pattern; each pattern is chosen once by the dynamic program
   (the level's grid and gaps, one note per step) and laid into its bars, only where a bar really has that onset.
   A density cap per level (notes a second over the bars that have notes: Easy 1.6, Medium 3.5, Hard 6) raises the
   cost of a note until it holds.
8. **Sections** from onset counts per bar: drop (kick and bass), break (no kick), build (the end of a break where the
   claps thicken), intro and outro (kick without bass before the first and after the last drop).
9. **Write**: the four tracks as Ogg Vorbis (kick 48 kb/s, clap and bass 56 kb/s, mono; other 96 kb/s stereo),
   `song.json`, `preview.wav`, `report.json`.

## Format and alignment

The engine streams WAV, FLAC, MP3, Ogg Vorbis and Ogg Opus. Ogg Opus was the first choice, but libopus in the only
WASM ffmpeg build available (`@ffmpeg/core` 0.12.10) crashes on any non-silent input, and ffmpeg's own Opus encoder is
experimental. Ogg Vorbis (libvorbis) has no pre-skip to get wrong: its decoded output starts at input sample 0.

Kick, clap and bass are mono (they sit in the centre of a house mix, and in the game they only duck on a miss); the
other track is stereo. That is about 1.5 MB a minute (see the numbers below).

The proof, at package time (`make-song`, recorded in `report.json` `alignment`): every track is decoded back by
ffmpeg's Vorbis decoder, by libvorbis (`@wasm-audio-decoders/ogg-vorbis`) and, when built, by stb_vorbis driven as
the engine drives it; each decode must be exactly the package's length in samples and cross-correlate against the
audio it was made from at 0 samples of lag over the first minute, or the package is not written. `check-songs`
repeats it later from a 16384-sample excerpt of each source kept in `report.json`, with a control (the kick track
moved by 64 samples must be caught).

## Accuracy against ground truth

Measured by `node tools/eval-accuracy.mjs` on 93 s synthetic songs at 124 BPM (188 beats; 162 kick, 104 clap and 177 bass
notes, 58 of the bass notes legato), read through the real import path: the `.stem.mp4` (AAC, 192 kb/s) decoded by
ffmpeg-wasm. The AAC priming (1024 samples) is measured against the true master and taken off before comparing.
Onsets count as found within 50 ms (and the 25 ms column); "quantized" is the onsets snapped to the detected grid
against the true notes, before any difficulty selection.

| Variant | Tempo found (true) | Tempo error | Beat error mean / max | Downbeat | Sections |
|---|---|---|---|---|---|
| plain | 124 (124) | 0% | 0.423 / 0.793 ms | right | all 6 right |
| drift | 124.19 (124.185) | 0.004% | 0.357 / 0.74 ms | right | all 6 right |
| bleed | 124 (124) | 0% | 0.432 / 0.782 ms | right | all 6 right |

| Variant | Lane | Precision | Recall | Recall at 25 ms | Mean timing error | Quantized precision / recall |
|---|---|---|---|---|---|---|
| plain | kick | 1 | 1 | 1 | 0.925 ms | 1 / 1 |
| plain | clap | 1 | 1 | 1 | 0.581 ms | 1 / 1 |
| plain | bass | 1 | 1 (legato 1) | 0.966 | 8.709 ms | 1 / 1 |
| drift | kick | 1 | 1 | 1 | 0.844 ms | 1 / 1 |
| drift | clap | 1 | 1 | 1 | 0.608 ms | 1 / 1 |
| drift | bass | 1 | 1 (legato 1) | 0.966 | 8.385 ms | 1 / 1 |
| bleed | kick | 1 | 1 | 1 | 0.911 ms | 1 / 1 |
| bleed | clap | 1 | 1 | 1 | 0.592 ms | 1 / 1 |
| bleed | bass | 1 | 1 (legato 1) | 0.966 | 8.794 ms | 1 / 1 |

Read from the WAV stems instead (no AAC), the numbers are the same to within 0.1 ms and 0.01 recall.
Drift is +0.3% over the song (124 to 124.37 BPM); bleed is the kick, low passed, at 0.3 of its level in the bass stem.
The synthetic song is clean by construction, so these numbers say the pipeline is right, not that a real mix is this
easy; the real songs below are the other half of the evidence.

### The first real songs

| Song | Prompted | Found | Notes easy / medium / hard | Notes a second (bars with notes) | On a re-found onset (easy / medium / hard) | Hard moved an 8th (control) | Size |
|---|---|---|---|---|---|---|---|
| Chicago Jack (171 s) | 124 | 126.11 | 220 / 294 / 456 | 1.38 / 1.8 / 2.79 | 1 / 1 / 0.987 | 0.377 | 4.45 MB (1.56 MB/min) |
| Late Night (210 s) | 120 | 121.01 | 192 / 336 / 430 | 1.31 / 1.84 / 2.36 | 1 / 0.997 / 0.986 | 0.063 | 5.08 MB (1.46 MB/min) |

Chicago Jack is not at a steady 124: its beats run from about 125.2 BPM at the start to 127.0 at the end (the track
drifts), and the beat fit follows it (median kick-to-beat distance 1.8 ms). Late Night holds 121.0.

## Dependencies (all in `tools/package.json`, development only: nothing here ships in the cart)

The song tooling is a separate development step: the packages below are fetched by `npm ci` into `tools/node_modules/`
(never committed) and run only when a song is made; none of their code is linked into the game or shipped in its cart,
which carries only the Ogg Vorbis tracks and charts they wrote. ffmpeg's GPL therefore covers the tool run, not the
game, which stays MIT like the rest of this repository.

| Package | License | Use |
|---|---|---|
| `@ffmpeg/core` 0.12.10 | GPL-2.0-or-later (ffmpeg built with GPL parts) | AAC decode/encode, Vorbis encode, in WebAssembly |
| `stem-mp4` 0.5.0 | MIT | NI Stems container read and write |
| `@wasm-audio-decoders/ogg-vorbis` 0.1.20 | MIT (libvorbis BSD; its `codec-parser` dependency LGPL-3.0-or-later) | the second Vorbis decoder for the alignment proof |
| stb_vorbis (the engine's copy, optional build) | public domain / MIT | the engine's decoder, for the alignment proof |
