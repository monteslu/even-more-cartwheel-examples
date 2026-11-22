# beatsync design

A house-music rhythm game on three buttons. Songs are instrumental house tracks split into stems; the player plays
the kick, the clap and the bass by hitting notes in time, and those parts of the song sound the way they are played.

This file is the contract between the song tooling (`tools/`) and the game (`src/`). Change it first when either side
needs something different.

## Play

- **Three buttons, one thumb.** The three lower face buttons: west, south, east (left, bottom, right of the diamond).
  Lanes from left to right: **kick** (west), **clap** (south), **bass** (east). Keyboard fallback: J, K, L. Touch:
  three wide zones across the bottom of the screen, in the same order.
- **One or two players** (below): one player on the three lanes, one player on two lanes (a pair of parts), or two
  players side by side, each on a pair on their own pad.
- **Never two notes at once.** A chart never asks for two buttons in the same moment, at any difficulty (two players
  can have a note each at once: they are on two pads).
- **The song reacts.** The "other" stem (chords, pads, stabs, the rest of the drums) plays at full volume, except
  where the chart plays notes made from it (a break, below): there it sits about 6 dB down between hits (easing over
  half a second), a hit on such a note punches the other track about 3 dB over the record and a miss dips it, so a
  stab rings on a hit and is cut short on a miss. In play the kick and clap tracks sit a little under the record
  between the player's hits (about -4.4 dB, the top above 7 kHz softened); the bass, 8 to 11 dB under the drums in
  these songs, sits lower (about -8 dB) and a hit lifts it about 3.5 dB over the record, so a bass note hit or missed
  is heard in the whole mix. A hit punches its track (kick and clap to full) and opens its top: the drum
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
    times against) and crosses the gate about 14% faster. The perspective does not change the timing windows (see Judgement). Easy's notes are drawn brighter too: its quarter notes pass any point of a
    river at most about twice a second, so they can stand out more than Medium's and Hard's eighths and sixteenths
    without a regular flicker over 3 Hz.
  - **Every effect tells the player something.** It shows what the music will do, or what the player just did. An
    effect that only repeats what is already heard (a ring on every kick of the record, sparks on every clap, a field
    of glyphs following the loudness) adds nothing and is not drawn.
  - **The land: the other track, seen coming.** Beside the rivers, out to the screen's edges, a landscape of
    characters that is the other stem (the chords, pads, stabs and synth lines the player does not play), read from
    the song's look (see "The look from the music"). It is a row every half beat riding down with the notes, so the
    music ahead is on screen as far ahead as the notes are: a loud stretch rises before it is heard, a break flattens.
    A row's height follows the track's loudness at its beat, its bass, mid and treble laid outward (bass beside the
    rivers, treble at the edges); its colour is the harmony sounding then, held from chord change to chord change and
    spread over the song's own range of harmonies, so a chord or key change is a new colour travelling down toward the
    gates. A stab puts lights on its row's ridge; a row where the harmony moves carries a crest of light. A faint sky
    above the horizon holds the harmony sounding now. The land stays under the rivers' light: the rivers are what the
    eye follows.
  - **The streak, where the eyes are.** The multiplier (x1 to x4, a step every 16 hits) sits under the middle gate
    with a bar of the hits to the next step. The streak charges the banks with light from the gates up, reaching the
    far end at x4, so a full streak is a highway lit to the horizon; each step sends a band of light up the rivers. A
    streak of every 50 is called out under the middle gate. A streak broken after a multiplier drains: its charge runs
    back down the banks in red over 0.7 s, the lit pieces fall away, the pad rumbles (harder for a higher
    multiplier), and "streak N lost" shows where the multiplier was.
  - **Playing well or badly.** A hit bursts at the gate in the river's colour: shards thrown up and out and a ring
    opening out, swelling in over 40 ms and fading over half a second (a Perfect throws more, faster and further, its
    ring wider and whiter); it blooms the gate and surges its river around and below it (the current brightens and
    widens, swelling and fading). The judgement word keeps one colour on every river: Perfect green, Good turquoise,
    Miss red (the results line too). A miss dims that instrument's river for the beat its track is down. Each section has its
    own palette (break cool cyan, build warming, drop hot magenta and orange), crossfading over a bar.
  - **The title** has the rivers spread wide round the title's box, the first song playing itself on them.
  - **Rules.** Every light swells and fades over several frames, never a whole-screen flick
    (the flash rules hold at 128 BPM, and romdev's picture check over whole songs on Hard). Characters large enough to
    read on a TV across a room. The HUD and menus in boxes over the field, like Detexto's. No screen shake (a hit or a
    miss rumbles the pad).

## One or two players

How people groove to a record: two hands on a table, low and high. So the second way to play is two lanes, and two
players jam together, each on two.

- **The parts.** The three-lane game plays kick, clap and bass. The two-lane game splits the song into two pairs:
  the **drums** (kick and clap; the hats ride the clap button, the clap track carries them) and the **groove** (bass
  and the **synth**: the other stem's chords, stabs and lines, playable only here). Together the two pairs are the
  whole track.
- **One player** chooses on the difficulty screen: three lanes (kick, clap, bass on West, South, East, as before), or
  two lanes on the drums or the groove. **Two players** each play one pair on their own pad (the drums and the groove
  by default; left or right swaps them), each at their own difficulty, and both press ready.
- **Buttons for two lanes.** A pair's left lane is West, its right lane South: one thumb rocking between two
  neighbouring buttons. East stays the three-lane game's bass.
- **Tempo on the shoulders.** L slower, R faster, 5% a press, 70% to 130%, either pad, in play only (the count-in shows
  the two buttons; the HUD the BPM as played and the percent, in the accent colour while it is not 100%). Every track
  takes the change in one engine command (cw_voices_set_tempo: the stems stay locked to the sample) with the pitch
  kept; the song clock counts song time, so the charts need nothing; the judgement windows are in real time (scaled
  by the tempo in song time); the device offset too. A hit scores its points times the tempo. Previews and the title
  play as recorded. Gate: tests/beatsync-tempo.mjs (sync at 120% with an open-loop bot, the kick heard on the clock,
  the pitch kept; controls: the bot at the song's own speed, the chart moved, the tempo by playback rate). Keyboard fallback: J and K. No touch layout of the game's own: a touch
  device gets the host's gamepad overlay, which draws every button the input map names.
- **Joining.** Play opens the shell's join screen: South takes a seat, Start plays (alone, Start plays at once). A
  second pad can also join with one press on the song select or the difficulties. A pad that goes away frees its
  seat.
- **More notes on two lanes.** With two buttons to follow instead of three, each level of a two-lane chart carries
  more notes a button than the three-lane chart of that level (Easy on the eighths with a quarter note a hand, Medium
  eighths on both hands, Hard sixteenths), and never fewer notes than the three-lane chart gives those parts.
- **Together, each with their own streak.** One score and one crowd; each player has their own streak and multiplier
  (their notes score at their own multiplier), their road's banks charge with it, and a miss breaks only their own
  (it drains on their road, "streak N lost" under it) while it still costs the crowd. The pad that missed rumbles. Each player's notes are judged in their own difficulty's windows, and a press
  only ever counts for its own player's lanes (another player's note is never your wrong button). The results show
  each player's part and accuracy; two players play for the song, so no best is kept for them. One player's bests
  are kept per difficulty for the three lanes and for each pair.
- **The rivers.** One player on a pair: two rivers further apart than the three-lane game's (two rivers of dense
  notes in one sixth of the screen were a regular flicker). Two players: two roads side by side, each player's pair on its
  own road converging to its own vanishing point (player 1 on the left), the land filling between the roads as well
  as outside them; the shared streak badge in the middle, over the land between the roads. The rivers glide
  to the layout chosen on the difficulty screen. The synth river is its own colour in every section's palette and
  its notes are hexagons. With two players at two difficulties the rivers show the slower one's beats ahead, so both
  read their notes at least as early as their own level would.
- **The mix.** A part nobody plays sits at full, like the record. When the synth is played the other track becomes
  a lane: under the record between hits, punched up by a hit, dipped by a miss.

## The song package

One directory per song under `songs/<song-id>/`, written by `tools/make-song.mjs`, read by the game:

```
songs/<song-id>/
  song.json        the song's facts and its charts
  kick.ogg         the drum stem's low band (the kick)
  clap.ogg         the drum stem's high band (clap, hats, the rest of the kit)
  bass.ogg         the bass stem
  other.ogg        the other stem
  vocals.ogg       optional: the vocals, for a song kept with them (played as recorded, never reacting)
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
- `look`: what the picture reads from the music (below), between `sections` and `charts`.
- `tracks.vocals`, optional: a song kept with its vocals (`tools/make-song.mjs --vocals`). The game plays it as
  recorded, on its own bus, never ducked or punched: the player plays the instruments, the singer sings. The songs
  that ship are instrumental and have none.
- The song list: the game finds every `songs/<id>/song.json` among its cart's assets when it starts (the host lists
  them, wasmcart `_filelist.txt`), in `songs/order.txt`'s order, then by artist and title. A cart unpacked to a folder
  gets a song by having its folder dropped into `assets/songs/`, no rebuild. Up to 512 songs; the save keeps the
  bests of the first 32 played (the shell caps a game's save at 6 KB).
- `pairs`: the two-lane charts, after `charts`: `{"drums": {"easy": [...], "medium": [...], "hard": [...]},
  "groove": {...}}`, notes as above with `lane` 0 kick, 1 clap (the drums), 2 bass, 3 synth (the groove).

## The look from the music

`tools/lib/look.mjs` analyses the other stem once, when the package is made, on the song's own beats, and writes
`song.json` `look.other`. Made ahead of time, it lets the game draw what is coming, not only what sounds now. The
kinds of data are the ones music visualizers feed their presets (band energies against their own average, a beat
grid, onsets), plus the harmony:

| Key | One value | What it is |
|---|---|---|
| `level` | a beat, 0..99 | the track's loudness over the beat, 0 at 36 dB under its loud beats |
| `bright` | a beat, 0..99 | its tone: the spectral centroid, about 150 Hz (dark pads) to 5 kHz (bright stabs) |
| `hue` | a beat, 0..359 | its harmony as an angle on the circle of fifths: each sounding pitch class at its place (C 0, G 30, D 60, ...), summed. Related chords sit close; a key change moves far |
| `tonal` | a beat, 0..99 | how clearly one harmony sounds (a held chord high, noise or a drum hit low) |
| `changes` | a list | the beats where the harmony moves (the chroma of the two beats after against the two before) |
| `bands` | a half beat, 3 each | bass (20-250 Hz), mid (250-2000) and treble (2-8 kHz), each against its own average over the song: 50 its usual energy, 100 twice it, at most 199 |
| `stabs` | pairs | the track's attacks: [beat position x 100, strength 1..9] |

The game turns loudness into a height against the song's own quiet and loud beats, holds the harmony from change to
change, and spreads it over the song's own range of harmonies (its mean at blue-violet). `check-songs` makes the look
again from the decoded other track and holds each beat's loudness to it (control: the look moved by two beats).

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
7. Bars that sound alike share one chart pattern, so a groove repeats and can be learned. **Medium plays all three
   lanes**: the drums own every beat, so left to the scores a Medium pattern takes kicks and claps and no bass. Where
   the bass plays in a pattern and the pick left it out, each of the bass's places is tried as a must-have note (the
   rest picked as usual around it) and the one that costs the pattern least is kept: at least one bass note in every
   such bar. Easy stays kick and clap (its quarter-note grid misses the bass's off-beats); Hard has the bass anyway.
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
   Medium at least 15% bass notes in a song with a bass line (control: the same chart with its bass taken out),
   the share of notes on real onsets (break notes from the other stem against the other track, and the break notes
   on their own), no gap over two bars and the first note within two bars of the first onset, the tracks aligned;
   controls that must fail: the chart moved by an 8th scores badly against the audio, a chart with a 30 s hole cut
   into it fails the gap rule, a chart without its first 30 s fails the first-note rule.

`tools/make-test-song.mjs` makes a synthetic house song with known notes (a kick, a clap, a bass line, a pad) as a
`.stem.mp4` or a stems directory, so the tooling is measured against ground truth and the game has a song before any
real one exists. The real songs are made by the author and split into stems with Loukai.

### Two lanes

The two-lane charts (`pairs`, `tools/lib/chart.mjs` `makePairCharts`) are made the same way from each pair's own
onsets: the drums from the kick and clap onsets plus the hats (weaker candidates on the clap, so Easy keeps the claps
and Medium and Hard add the hats between them), from the drums' first beat; the groove from the bass onsets and the
other stem's onsets (the synth) from the top. Their levels (`LEVELS2`) are denser than the three-lane ones (Easy:
eighths, a quarter note a hand; Medium: eighths; Hard: sixteenths, up to 7 notes a second), each level keeps every
note the three-lane chart of that level gives its parts where its gaps allow, and the break fill uses the pair's own
unchosen onsets only: where neither part plays, that player rests. `check-songs` holds them (see `docs/TOOLS.md`).

## Timing in the game

- The four tracks start on the same mixer frame (`cw_play_desc.start_frame`), so they stay sample-locked.
- The song clock is `cw_voice_frames_played` of the other track, divided by its rate, moved on smoothly between
  mixer blocks by the frame time, plus the device offset. Notes are drawn and judged on that clock.
- A test run (deterministic, no real clock) must still play a song to the end on the clock, so the gates can run a
  bot that hits every note on time and gets every Perfect, and a control bot hitting 50 ms late that gets no Perfect.
