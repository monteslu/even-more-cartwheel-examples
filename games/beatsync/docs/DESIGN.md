# beatsync design

A house-music rhythm game on three buttons. Songs are instrumental house tracks split into stems; the player plays
the kick, the clap and the bass by hitting notes in time, and those parts of the song sound the way they are played.

This file is the contract between the song tooling (`tools/`) and the game (`src/`). Change it first when either side
needs something different.

## Play

- **Three buttons, one thumb.** The three lower face buttons: west, south, east (left, bottom, right of the diamond).
  Lanes from left to right: **kick** (west), **clap** (south), **bass** (east). Keyboard fallback: J, K, L. Touch:
  three wide zones across the bottom of the screen, in the same order.
- **Never two notes at once.** A chart never asks for two buttons in the same moment, at any difficulty.
- **The song reacts.** The "other" stem (chords, pads, stabs, the rest of the drums) plays at full volume all the
  time. The kick, clap and bass tracks play at full volume while the player keeps hitting them; a miss ducks that
  track (a short fade down, about 40 ms, with a low-pass sweep if the engine's bus effects allow it) until the next hit
  in its lane brings it back. A wrong button is a miss of the note it was meant for.
- **Judgement.** A hit is judged against the note's time in the song, not the frame: Perfect within 35 ms, Good within
  70 ms, else Miss. A per-device offset from the calibration screen is added (saved).
- **Scoring.** Points per Perfect and Good, a combo, a multiplier that grows with the combo, a grade at the end, the
  best score and grade saved per song and difficulty.
- **Difficulties.** Easy, Medium, Hard per song (see Charts).
- **Screens.** Title (the game behind it, never a bare screen), song select (title, BPM, best grade per difficulty, a
  preview of the song), difficulty, play, results, settings (calibration, volumes). The engine's game shell gives the
  title, pause, settings and saves.
- **Calibration.** Tap along to a click; the mean offset of the taps becomes the device offset.
- **Not a guitar game.** The player is the rhythm section of a house track, closer to playing a drum machine live than
  a lead part. The groove is the content: house loops in 1, 2 and 4 bars that change at the builds, drops and breaks,
  so the fun is locking into a loop and adapting when the section changes. Doing well sounds like the track getting
  fuller; a miss thins the mix, never a harsh error sound.
- **Look: a step sequencer in neon ASCII.** Every frame is drawn as glowing characters on black (the ASCII pass from
  Detexto, `games/detexto/src/ascii.c`, copied in and adapted): the game draws a bold
  scene of shapes, glow and colour to an offscreen target and the pass turns each cell into a character by brightness
  (` .:-=+*#%@`), tinted neon. The effects are the point: make them really cool.
  - **The sequencer ring.** 16 steps a bar round a circle, one ring per instrument (kick inner, clap middle, bass
    outer); upcoming notes are dense glowing blocks; a playhead sweeps round with a fading tail. The hit point is 12
    o'clock, where a note swells from `+` to `#` to `@` exactly on its beat (the cue is the character growing denser,
    not only its place). The next section's pattern fades in a bar ahead.
  - **The music drives the field.** Each kick sends a shockwave of characters out from the centre (`@ # * .`), so the
    screen breathes on four-on-the-floor; a clap throws sparks (`' * +`) that drift off; the bass is a waveform of
    characters along the bottom that thickens while the bass plays and thins when it is missed out; the other stem's
    energy sets a slow field of faint glyphs behind everything, sparse in a break and dense in a drop.
  - **Playing well or badly.** A miss dims that instrument's ring to `.` until it is back. A streak climbs the
    character set and the brightness; a long streak in a drop fills the screen dense and bright. Each section has its
    own palette (break cool cyan, build warming, drop hot magenta and orange), crossfading over a bar.
  - **Rules.** Every pulse swells and fades and the field brightens over several frames, never a whole-screen flick
    (the flash rules hold at 128 BPM). Characters large enough to read on a TV across a room. The HUD and menus in
    boxes over the field, like Detexto's. No screen shake (a hit or a miss rumbles the pad).

## The song package

One directory per song under `songs/<song-id>/`, written by `tools/make-song.mjs`, read by the game:

```
songs/<song-id>/
  song.json        the song's facts and its charts
  kick.ogg         the drum stem's low band (the kick)
  clap.ogg         the drum stem's high band (clap, hats, the rest of the kit)
  bass.ogg         the bass stem
  other.ogg        the other stem (vocals are never used)
  preview.wav      not shipped: the mix with a click on every Hard note, for checking a chart by ear
  report.json      not shipped: the checks' numbers
```

All four tracks have the same sample rate and exactly the same length in samples (`song.json` `length_samples`), and
sample 0 of each is the same moment of the song (the tooling proves it by decoding them back and cross-correlating).
The tracks are Ogg Vorbis: kick, clap and bass mono, other stereo (`docs/TOOLS.md` says why and what was measured). The format is whatever the
engine streams that keeps that alignment (Ogg Vorbis or Ogg Opus; FLAC if neither can); the tooling picks it and
`song.json` names the files.

`song.json`:

```json
{
  "format": 1,
  "id": "late-night",
  "title": "Late Night",
  "artist": "",
  "bpm": 120.0,
  "sample_rate": 48000,
  "length_s": 182.4,
  "tracks": { "kick": "kick.ogg", "clap": "clap.ogg", "bass": "bass.ogg", "other": "other.ogg" },
  "beats": [0.512, 1.012, 1.512],
  "downbeat": 0,
  "sections": [{ "t": 0.512, "kind": "intro" }, { "t": 16.51, "kind": "drop" }],
  "charts": {
    "easy":   [{ "t": 0.512, "step": 0, "lane": 0 }],
    "medium": [],
    "hard":   []
  }
}
```

- `beats`: the time in seconds of every beat (quarter note) from the first, tracked from the audio (so it holds even
  if the tempo drifts a little). `downbeat`: the index in `beats` of the first bar's first beat.
- A note: `t` its time in seconds, `step` its 16th-note index from beat 0 (step 4 is beat 1), `lane` 0 kick, 1 clap,
  2 bass. Notes are sorted by `t`, at most one per step.
- `sections`: where the song's parts start (intro, build, drop, break, outro), from the audio, for the visuals.

## Charts (the tooling's job)

1. Decode the `.stem.mp4` (NI Stems from Loukai: master, drums, bass, other, vocals) to PCM. Open source only, JS
   and WASM, nothing installed on the host.
2. Split the drum stem with a crossover at about 150 Hz: below is the kick track, above is the clap track (the clap's
   onsets are found in about 1 to 5 kHz so hats do not count).
3. Tempo and beats: an onset-strength envelope (spectral flux) of the kick band, autocorrelated over 115 to 135 BPM;
   the phase that lines the grid up with the most kick energy; a dynamic-programming beat tracker that follows the
   audio near the predicted beats. Bars from the claps on 2 and 4.
4. Onsets per lane: spectral flux with an adaptive threshold (moving median plus a multiple of the deviation), peak
   picking with a minimum gap; for the bass also pitch changes (YIN), since legato notes have no attack.
5. Quantize to 16ths; an onset more than about 40% of a 16th from the grid is dropped.
6. Difficulties, one note per step, the clap winning beats 2 and 4, the kick 1 and 3, the bass off-beats and fills:

   | Level | Grid | Same lane gap | Lane change gap |
   |---|---|---|---|
   | Easy | quarters | 1 beat | 1 beat |
   | Medium | 8ths | 1/8 | 1/4 |
   | Hard | 16ths | 1/16 | 1/8 |

   Each candidate note scores its strength, its place in the bar (downbeat, beat, 8th, 16th) and how unusual it is
   (a fill beats the hundredth plain kick); a dynamic program picks the best set that keeps the level's gaps.
7. Bars that sound alike share one chart pattern, so a groove repeats and can be learned.
8. Checks (`tools/check-songs.mjs`, also in the repository's CI): notes per second per level, no two notes in a step, the gaps held,
   the share of notes on real onsets, the tracks aligned; a control that must fail: the chart moved by an 8th scores
   badly against the audio.

`tools/make-test-song.mjs` makes a synthetic house song with known notes (a kick, a clap, a bass line, a pad) as a
`.stem.mp4` or a stems directory, so the tooling is measured against ground truth and the game has a song before any
real one exists. The real songs are made by the author and split into stems with Loukai.

## Timing in the game

- The four tracks start on the same mixer frame (`cw_play_desc.start_frame`), so they stay sample-locked.
- The song clock is `cw_voice_frames_played` of the other track, divided by its rate, moved on smoothly between
  mixer blocks by the frame time, plus the device offset. Notes are drawn and judged on that clock.
- A test run (deterministic, no real clock) must still play a song to the end on the clock, so the gates can run a
  bot that hits every note on time and gets every Perfect, and a control bot hitting 50 ms late that gets no Perfect.
