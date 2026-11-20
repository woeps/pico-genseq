# GenSeq User Manual

GenSeq is a standalone generative MIDI sequencer. It plays up to 15 patterns at
once on its MIDI output, and each pattern is built from three independent
streams: a **gate set** (the rhythm), a **pitch set** and a **velocity set**.
On top of that, every pattern has **generation parameters** that shape the
material while it plays: step probability, a scale constraint, velocity
humanization, a deterministic seed and stream coupling.

This manual covers firmware milestone M3 (generative engine expansion).

## 1. Connecting

| Connection | Purpose |
|---|---|
| USB keyboard | The control surface. Every action is a key press. Holding a key repeats it. |
| MIDI OUT (5-pin DIN) | Notes plus MIDI Clock, Start and Stop for your synth or drum machine. All patterns play on MIDI channel 1. |
| Power (USB) | Powers the device. Your setup is kept across power cycles once you save it. |

GenSeq starts in the TEST view (label `tst`) with your last saved setup; press
**F3** to go to PATTERNS. On the very first boot (nothing saved yet) it starts
with one default pattern at 120 BPM.

## 2. The display

The 16×16 LED matrix shows one view at a time. Editor views all use the same
layout:

```
 rows 0-4   LABEL  (yellow)   which field you are editing, e.g. Prb, Scl, Hmn
            status pixels in the top-right corner (see below)
 rows 6-10  VALUE  (green)    the field's current value: a number, a note or text
```

Numbers always show three digits (`050` = 50). Notes show the letter on the
left, the octave on the right, and a single dot at the right edge for a sharp
(`C#4` = letter C, dot, digit 4).

**Status pixels** (top row, right side):

| Pixel | Meaning |
|---|---|
| (15,0) red | You have an uncommitted change in this editor. Enter keeps it, Esc undoes it. |
| (14,0) green / red | Pitch editor only, when a scale is active: the shown pitch is in / out of the scale. |
| (13,0) green / dim red | Gate grid only: the selected step's gate is on / off. |

**Save banners** fill the whole matrix for 1.5 seconds:

| Colour | Meaning | Onboard LED |
|---|---|---|
| Green | Save succeeded | on |
| Red | Save failed. Your setup is still active, just not stored. | fast blink |
| Yellow | At boot: a saved setup was found but could not be loaded. GenSeq started with defaults. | slow blink |

## 3. Global keys

These work in every view.

| Key | Action |
|---|---|
| Space | Play / Stop. Play always starts every pattern from its first step. Stop releases every sounding note. |
| Ctrl+S | Save the whole setup (patterns, BPM, all parameters) to memory. Playback keeps running. |
| F1 | TEST view |
| F2 | SETTINGS view (tempo) |
| F3 | PATTERNS view |

## 4. PATTERNS view (F3), label `PTN`

The overview of all patterns. Each pattern is a small square in a grid of 5
columns by 3 rows (pattern 1 top-left, reading order).

- **Red square:** a pattern. All patterns play.
- **Yellow line:** the empty "add" slot after the last pattern.
- **White pixel:** the selection. Its corner inside the selected square says which
  stream you will open: top-left = GATE, top-right = PITCH, bottom-left = VELOCITY.

| Key | Action |
|---|---|
| Ctrl+Left / Ctrl+Right | Select the previous / next pattern |
| Ctrl+Up / Ctrl+Down | Select the pattern one row up / down |
| Up / Down / Left / Right | Choose the stream: from GATE, Right = PITCH and Down = VELOCITY; Left or Up returns to GATE |
| Enter | Open the editor for the selected stream. On the add slot: add a new pattern. |
| Delete | Delete the selected pattern |

## 5. How editors work

Every editor works the same way:

- **Left / Right** moves between fields. The label tells you where you are.
- **Up / Down** changes the value. Hold **Shift** for big steps (×10 for numbers,
  an octave for pitches).
- Changes are **heard immediately** while you edit, so you can tune by ear.
- **Enter** commits: the change becomes part of the pattern (and is included in the next save).
- **Esc** undoes everything since the last Enter. Press Esc again (with nothing to
  undo) to go back to PATTERNS. Leaving an editor any other way also undoes
  uncommitted changes.

Each generation parameter has a "neutral" value that switches the feature off
(probability 100, scale `cro`, humanize 0, seed `off`, coupling `Ind`). A pattern
with every parameter neutral plays exactly like a plain step sequence.

## 6. GATE editor (the rhythm)

Opens on `Alg`. Fields from left to right:

| Label | Value | Meaning |
|---|---|---|
| `Alg` | `euc` | Rhythm generator. Euclidean is the only one in this version. |
| `StP` | 1–64 | Steps in the pattern |
| `PLS` | 0–steps | Pulses: how many steps are hits. They are spread as evenly as possible (Euclidean). |
| `rot` | 0–steps-1 | Rotation: shifts the hits around the pattern |
| `nLn` | 32, 16, 8, 4, 2, 1 | Step length as a note value: 16 = sixteenth notes, 4 = quarter notes |
| `LEn` | 0–100 | Gate length, percent of a step that the note is held |
| `Prb` | 0–100 | **Step probability** for the whole pattern, in percent. 100 = every hit plays; 50 = each hit has an even chance; 0 = silent. |
| `s0`, `s1`, ... | grid | **Per-step grid**, one field per step (up to 16) |

**The per-step grid.** Moving right from `Prb` enters the grid at step `s0`.
Left/Right walks the steps; Left from `s0` goes back to `Prb`.

- **Enter** toggles the selected step's gate on or off (pixel (13,0) green = on).
  Because Enter toggles on the grid, move Left onto `Prb` (or any other field)
  and press Enter there to commit.
- **Up / Down** sets this step's own probability: `inh`, 0, 10, 20 ... 100.
  `inh` (inherit) means "use the pattern's `Prb`". Any number overrides `Prb` for
  this step only. Example: `Prb 50` with step 0 at 100 gives a solid downbeat and
  a sparse rest of the bar.

Changing `StP`, `PLS` or `rot` regenerates the gate pattern, but keeps every
per-step probability you set.

## 7. PITCH editor

Opens on `Cnt`. The scale fields are to the left of it, the pitches to the right.

| Label | Value | Meaning |
|---|---|---|
| `Scl` | `cro`, `maj`, `min` | **Scale constraint**: chromatic (off), major, natural minor |
| `rut` | C ... B | Root of the scale (sharp dot for C#, D#, F#, G#, A#) |
| `qnt` | `nea`, `dwn`, `up`, `skp` | What happens to a pitch outside the scale: snap to the nearest scale note, snap down, snap up, or skip the step (silent) |
| `Cnt` | 1–16 | How many pitches are in the set |
| `Ord` | `Fwd`, `Bwd`, `Pnd`, `Rnd` | Playing order: forwards, backwards, pendulum, random |
| `p0`, `p1`, ... | note | The pitches. Up/Down = semitone, Shift = octave. |

The scale constraint changes what is **played**, never what is **stored**: your
pitch set stays as you entered it, and turning the scale back to `cro` restores
it exactly. While a scale is active, the pixel at (14,0) shows whether the pitch
you are looking at is in the scale (green) or will be changed (red).

## 8. VELOCITY editor

Opens on `Cnt`. Pattern-wide generation fields are to the left of it.

| Label | Value | Meaning |
|---|---|---|
| `Hmn` | 0–64 | **Humanization**: each note's velocity varies randomly by up to ± this amount. Results stay between 1 and 127. |
| `Sed` | `off`, 0, 1, 2 ... | **Deterministic seed** for this pattern (see below) |
| `Cpl` | `Ind`, `Lck` | **Coupling**: `Ind` = velocity set runs on its own; `Lck` = each pitch position always gets the same velocity |
| `Cnt` | 1–16 | How many velocities are in the set |
| `Ord` | `Fwd`, `Bwd`, `Pnd`, `Rnd` | Playing order |
| `v0`, `v1`, ... | 0–127 | The velocities |

**Seed.** Everything random in a pattern (step probability, humanization and
`Rnd` playing order) comes from that pattern's random generator.

- With `Sed off`, the pattern is different every time you press Play.
- With a seed number, the pattern plays **the same "random" performance every
  time**: after Stop and Play, after saving, and after a power cycle. Different
  numbers give different performances. Changing the seed while playing restarts
  the sequence from that seed.
- From `off`, Up gives seed 0; below 0 goes back to `off`.

**Coupling** matters when the pitch and velocity sets have different lengths.
With 4 pitches and 3 velocities, `Ind` lets the accents drift across the melody
(a 12-step cycle), while `Lck` ties velocity to the pitch position so the first
pitch is always accented the same way.

## 9. SETTINGS view (F2), label `bPM`

Up/Down changes the tempo by 1 BPM, Shift+Up/Down by 10. Range 40–255. The change
applies immediately and is included in the next save.

## 10. TEST view (F1), label `tst`

A simple value (0–99) adjusted with Up/Down (Shift = ×10). It has no musical
effect; it exists to check the keyboard and display. This is the view GenSeq
starts in.

## 11. Saving and loading

- **Ctrl+S** stores the complete setup: patterns, their three streams, all
  generation parameters and the tempo. Only committed values are saved, so press
  Enter in an open editor first.
- Saving does not interrupt playback.
- At power-up GenSeq loads the last save automatically. If nothing was saved it
  starts with defaults silently. If a save exists but cannot be read, the yellow
  banner appears and defaults are used.

## 12. Quick start

1. Connect MIDI OUT to a synth on channel 1, press **F3** (PATTERNS) and then
   **Space**. You hear the
   default pattern: four notes (C4 G4 A4 C5) on a 4-of-16 Euclidean rhythm.
2. **Enter** opens the gate editor: Right twice to `PLS`, Up to add hits.
3. Right to `Prb`, Down to 70: the rhythm starts to breathe. Press Enter.
4. **Esc, Right, Enter** opens the pitch editor: Left three times to `Scl`, Up to
   `maj`. Now edit the pitches freely (Right to `p0` ...): they always land in C major.
5. In the velocity editor set `Hmn` to 15 for a looser feel, then `Sed` to a number
   once you like what you hear. Press Enter.
6. **Ctrl+S** to keep it.

## 13. Not in this version

- Muting a single pattern from the keyboard. All patterns play; Delete removes one.
- Per-pattern MIDI channels. All patterns send on channel 1.
- Rhythm generators other than Euclidean.
