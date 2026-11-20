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
  time and never changes. In play the kick, clap and bass tracks sit a little under the record between the player's
  hits (about -4.4 dB, the top above 7 kHz softened), and a hit punches its track to full and opens its top: the drum
  or bass note just played stands out, then falls back over about a quarter second (on Easy, whose kicks are on 1 and
  3, the kicks the player plays stand about 4 dB over the ones on 2 and 4). A miss drops only that track (to about
  -13 dB, with a low-pass at 700 Hz) and it comes back by itself over one beat, or at once on the next hit in its
  lane, so a few misses across the lanes never empty the mix. A wrong button never misses a note: it breaks the combo, and the note it was meant for can then only be Good; two
  or more buttons on one tick (no chart has two notes on one tick) are a mash, scored Good at best with no combo.
  The song's own level changes only when the crowd leaves (the failed song fades out); the title and the song
  select's preview play every track at full.
- **No waiting.** Play starts 8 beats before the first note of the level chosen (on the song's beats; from the top
  when that note is within about 8 beats and two seconds of it), after the 4 s count-in; the intro is still heard in
  the song select's preview. The charts leave no long holes (see Charts). If a wait of more than 12 beats remains
  between two notes, the rivers' banks fill with light from the gate up as it passes, and over its last four bars a
  countdown (bars, then the last bar's beats) says when the notes come back onto the rivers; it fades out as they do.
- **Judgement.** A hit is judged against the note's time in the song, not the frame, in windows a person on a
  living-room setup can meet: Perfect within 75 ms and Good within 150 ms on Easy, 55 and 110 on Medium, 35 and 85 on
  Hard (its Perfect stays tight), else Miss. Notes are at least 465 ms apart on Easy, 228 on Medium and 114 on Hard,
  and a press takes the nearest note in its own lane. A per-device offset from the calibration screen is added (saved).
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
- **Look: a note highway in neon ASCII.** Every frame is drawn as glowing characters on black (the ASCII pass from
  Detexto, `games/detexto/src/ascii.c`, copied in and adapted): the game draws a bold scene of shapes, glow and colour
  to an offscreen target and the pass turns each cell into a character by brightness (` .:-=+*#%@`), tinted neon. The
  effects are the point: make them really cool.
  - **Three rivers, laid out as the buttons.** One river per instrument, side by side with clear gaps between them and
    centred, the HUD to the sides: **kick on the left** (the west button), **clap in the middle** (south), **bass on the
    right** (east). Which button a note wants is where its river is: position alone carries the mapping, so no button
    icon stays on screen in play (the count-in shows each button under its river, then they go).
  - **The current and the gate.** Each river is a current of dim characters streaming down at the notes' speed between
    two banks, crossed by a faint line on every beat (brighter on the bar). A gate, a bright bar across the river, marks
    the hit line near the bottom of the screen. The highway recedes in true perspective toward a vanishing point
    above the screen: a river's far end is a quarter of its width at the gate, high up the screen, and the current,
    banks and beat lines fade into a haze there, so the road reads as going away into the dark.
  - **The notes** ride the current down and reach the gate exactly on their beat (kick round, clap a diamond, bass a
    bar across its river). A note takes the river's perspective: a quarter of its gate size at the far end, growing
    all the way to the gate. Its brightness is a little higher far off (there it is smaller than a character and needs
    the light to show) and steady over its last beat, so its light near the gate grows only with its size, never in a
    burst on the beat: a run of notes riding down stays a steady light, not a regular flicker. How far ahead a river
    shows notes is set per difficulty: 8 beats on Easy, 6.4 on Medium, 5.2 on Hard (4, 3.2 and 2.6 seconds at 120
    BPM), twice what a flat river showed. The perspective packs the far beats toward the horizon, so the near part
    reads as it did: a note covers its last beat within about 10% of the distance it did before (what the player
    times against) and crosses the gate about 14% faster. The timing windows do not change. Easy's notes are drawn brighter too: its quarter notes pass any point of a
    river at most about twice a second, so they can stand out more than Medium's and Hard's eighths and sixteenths
    without a regular flicker over 3 Hz.
  - **The music drives the field.** Each kick sends a shockwave of characters across the field from below the gates,
    faint behind the rivers, so the screen breathes on four-on-the-floor; a clap throws sparks (`' * +`) off the clap's
    gate that drift and fall; the bass is a waveform of characters along the bottom, under the gates, that thickens
    while the bass plays and thins when it is missed out; the other stem's energy sets a slow field of faint glyphs
    behind everything, sparse in a break and dense in a drop.
  - **Playing well or badly.** A hit blooms the gate and surges its river around and below it (the current brightens
    and widens, swelling and fading). A miss dims that instrument's river for the beat its track is down. A streak
    brightens the rivers and the field; a long streak in a drop fills the screen dense and bright. Each section has its
    own palette (break cool cyan, build warming, drop hot magenta and orange), crossfading over a bar.
  - **The title** has the rivers spread wide round the title's box, the first song playing itself on them.
  - **Rules.** Every pulse swells and fades and the field brightens over several frames, never a whole-screen flick
    (the flash rules hold at 128 BPM, and romdev's picture check over whole songs on Hard). Characters large enough to
    read on a TV across a room. The HUD and menus in boxes over the field, like Detexto's. No screen shake (a hit or a
    miss rumbles the pad).

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
   audio near the predicted beats. Bars from the claps on 2 and 4. Where the song starts before the drums (an intro
   of bass, hats or chords), the grid is carried back over it, locked to those onsets, so the intro can be charted.
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
8. **No long silences in a chart.** On every level, no gap between two notes is longer than two bars, and the first
   note comes within a bar of the song's first onset (the game starts play a few beats before the first note, so an
   intro is played from where it starts). Where the chart above leaves a longer gap (a break, a drumless intro), a
   break fill adds notes from what plays there, in this order: the lanes' own onsets the chart left out (claps,
   bass), then the hats on the clap lane, then, only where the drums and the bass are both silent, the onsets of the
   other stem (chord changes, stabs, synth lines) on the **bass lane**. The bass lane is the song's pitched lane, and
   the other stem's chords and lines are pitched parts that move with it; the kick lane stays the four on the floor
   and the clap lane the backbeat, so a drop still reads as the drums coming in. A break is sparser than a drop: the
   fill uses one grid step coarser than its level (Easy half notes, Medium quarters, Hard 8ths), keeps the level's
   gap rules and one note per step, and only places a note where an onset really is. Easy may leave up to four bars
   only where no track has an onset at all.
9. Checks (`tools/check-songs.mjs`, also in the repository's CI): notes per second per level, no two notes in a step, the gaps held,
   the share of notes on real onsets (break notes from the other stem against the other track, and the break notes
   on their own), no gap over two bars and the first note within two bars of the first onset, the tracks aligned;
   controls that must fail: the chart moved by an 8th scores badly against the audio, a chart with a 30 s hole cut
   into it fails the gap rule, a chart without its first 30 s fails the first-note rule.

`tools/make-test-song.mjs` makes a synthetic house song with known notes (a kick, a clap, a bass line, a pad) as a
`.stem.mp4` or a stems directory, so the tooling is measured against ground truth and the game has a song before any
real one exists. The real songs are made by the author and split into stems with Loukai.

## Timing in the game

- The four tracks start on the same mixer frame (`cw_play_desc.start_frame`), so they stay sample-locked.
- The song clock is `cw_voice_frames_played` of the other track, divided by its rate, moved on smoothly between
  mixer blocks by the frame time, plus the device offset. Notes are drawn and judged on that clock.
- A test run (deterministic, no real clock) must still play a song to the end on the clock, so the gates can run a
  bot that hits every note on time and gets every Perfect, and a control bot hitting 50 ms late that gets no Perfect.
