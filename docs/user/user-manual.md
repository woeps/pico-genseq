# GenSeq User Manual

GenSeq is a standalone generative MIDI sequencer. It plays up to 15 patterns at
once on its MIDI output. Each pattern combines three sets: a **gate set** (the
rhythm), a **pitch set** (the notes) and a **velocity set**. The sets live in
three libraries of 16 sets each, and a pattern simply points at one set of each
kind. Several patterns can use the same set, for example three patterns playing
the same melody in three different rhythms. Editing a set changes it for every
pattern that uses it.

On top of that, the sets carry **generation parameters** that shape the material
while it plays: step probability (gate set), a scale constraint (pitch set),
velocity humanization, a deterministic seed and stream coupling (velocity set).

This manual covers firmware milestone M3 (generative engine expansion) with
shared sets.

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

![Editor layout on the LED matrix](img/layout.svg)

The picture shows the pitch editor on field `p0` (note C4) with a scale active. All screenshots in this manual are rendered from the firmware's own display code.

**Status pixels** (top row, right side):

| Pixel | Meaning |
|---|---|
| (15,0) red | You have an uncommitted change in this editor. Enter keeps it, Esc undoes it. |
| (14,0) green / red | Pitch editor only, when a scale is active: the shown pitch is in / out of the scale. |
| (13,0) green / dim red | Gate grid only: the selected step's gate is on / off. |

![Status pixels](img/status-pixels.svg)

**Set rows** (the two bottom rows, in the GATE, PITCH and VELOCITY editors):

| Row | Meaning |
|---|---|
| 15 | **Set map**: one pixel per set of this kind, set 0 at the left, set 15 at the right. Dim green = the set exists, white = the set you are editing, dark = free. |
| 14 | **Users**: one red pixel per pattern that uses the set you are editing, pattern 1 at the left. Two or more red pixels mean the set is shared, and your edit changes all of those patterns. No red pixel means no pattern uses it. |

A short word in the value area answers a set action that was refused (see
section 5). It disappears at your next key press.

![Set rows with a shared set](img/set-rows.svg)

In this example gate set 0 is used by patterns 1 and 2 (two red pixels), and three gate sets exist (set 0 is white).

**Save banners** fill the whole matrix for 1.5 seconds:

| Colour | Meaning | Onboard LED |
|---|---|---|
| Green | Save succeeded | on |
| Red | Save failed. Your setup is still active, just not stored. | fast blink |
| Yellow | At boot: a saved setup was found but could not be loaded. GenSeq started with defaults. | slow blink |

![Save banners](img/banners.svg)

## 3. Global keys

These work in every view.

| Key | Action |
|---|---|
| Space | Play / Stop. Play always starts every pattern from its first step. Stop releases every sounding note. |
| Ctrl+S | Save the whole setup (patterns, sets, BPM, all parameters) to memory. Playback keeps running. |
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
- **Bottom three rows:** which sets the selected pattern uses. Row 13 is the gate
  library, row 14 the pitch library and row 15 the velocity library, set 0 at the
  left. Dim green = the set exists, white = the set this pattern uses.

![PATTERNS view with three patterns, pattern 2 selected](img/patterns.svg)

Here pattern 2 is selected. The white pixel moves inside its square to show the stream: top-left GATE, top-right PITCH, bottom-left VELOCITY. In row 13 (gate library) set 1 exists but pattern 2 uses set 0 (white); the pitch and velocity libraries each hold only set 0.

![Add slot selected](img/patterns-add.svg)

With the add slot selected, the white pixel sits in the slot's lower-right corner and Enter adds a pattern.

| Key | Action |
|---|---|
| Ctrl+Left / Ctrl+Right | Select the previous / next pattern |
| Ctrl+Up / Ctrl+Down | Select the pattern one row up / down |
| Up / Down / Left / Right | Choose the stream: from GATE, Right = PITCH and Down = VELOCITY; Left or Up returns to GATE |
| Shift+Left / Shift+Right | Switch the selected stream of this pattern to the previous / next existing set (wraps around). You hear the change at once; the pattern keeps its place in the bar. |
| Enter | Open the editor on the set this pattern uses for the selected stream. On the add slot: add a new pattern. |
| Delete | Delete the selected pattern. Its sets stay in the libraries. |

A **new pattern** starts on the lowest-numbered existing set of each kind, so at
first it shares those sets with other patterns. To give it its own material, see
"Giving a pattern its own set" in section 5.

## 5. Sets and sharing

There are three libraries: 16 gate sets, 16 pitch sets and 16 velocity sets,
numbered 0 to 15. The GATE, PITCH and VELOCITY editors always edit **a set**, not
a pattern. They open on the set that the selected pattern uses, and the set rows
at the bottom of the display (section 2) show which set that is and which patterns
use it.

These keys work in all three editors:

| Key | Action |
|---|---|
| Ctrl+Left / Ctrl+Right | Edit the previous / next existing set of this kind (wraps around) |
| Ctrl+N | Create a new set with default content and edit it |
| Ctrl+D | Copy the set you are editing into a new set and edit the copy |
| Ctrl+Delete | Delete the set you are editing |
| Ctrl+G | Go to a pattern that uses this set |

Switching sets, Ctrl+N and Ctrl+D undo an uncommitted change first, just like
leaving the editor. Ctrl+D copies the last committed version. Creating, copying
and deleting a set never changes which sets the patterns use.

**Refused actions.** The value area shows a short word instead:

| Word | Meaning |
|---|---|
| `ful` | The library already holds 16 sets. Delete an unused one first. |
| `use` | The set cannot be deleted because a pattern uses it. Switch those patterns to another set first. Every library keeps at least one set, because there is always at least one pattern using it. |
| `unu` | Ctrl+G: no pattern uses this set. |

![Refusal words and the go-to picker](img/notices.svg)

The last picture is the Ctrl+G picker (`gto`) with patterns 1 and 2 as candidates (pattern 1 dim purple, the highlighted pattern 2 white); the number shows the highlighted pattern.

**Editing a shared set.** Two or more red pixels in row 14 mean the set is
shared, so every change you make is heard on all of those patterns at once. The
patterns still run independently: each one keeps its own position in the set and,
in `Rnd` order, its own random sequence. Because the generation parameters belong
to the sets, patterns that share a set also share them. For example, patterns
using the same pitch set share its scale.

**Go to a pattern (Ctrl+G).** If exactly one pattern uses the set, GenSeq jumps
straight to it in the PATTERNS view. If several do, a picker opens: the label
reads `gto`, the number is the pattern (pattern 1 = `001`), and row 14 shows every
pattern that uses the set, the highlighted one in white. Left/Right (or Up/Down)
moves between them, Enter goes to the highlighted pattern, Esc closes the picker
and leaves your editing untouched. Going to a pattern undoes an uncommitted
change, like leaving the editor.

**Giving a pattern its own set.**

1. In PATTERNS, select the pattern and the stream, then press Enter to open its set.
2. Press **Ctrl+D**. The editor now shows a copy (a new white pixel in row 15) that
   no pattern uses yet.
3. Edit the copy and press Enter to commit it.
4. Press Esc to return to PATTERNS and use **Shift+Right** (or Shift+Left) until
   row 13, 14 or 15 shows the copy in white for this pattern.

**Example: one melody, three rhythms.** Add three patterns. They start on the same
sets. Give patterns 2 and 3 their own gate sets as above and make the three rhythms
different. Leave all three on the same pitch set: every melody edit is now heard in
all three rhythms.

## 6. How editors work

Every editor works the same way:

- **Left / Right** moves between fields. The label tells you where you are.
- **Up / Down** changes the value. Hold **Shift** for big steps (×10 for numbers,
  an octave for pitches).
- Changes are **heard immediately** while you edit, so you can tune by ear.
- **Enter** commits: the change becomes part of the set, for every pattern that uses it
  (and is included in the next save).
- **Esc** undoes everything since the last Enter. Press Esc again (with nothing to
  undo) to go back to PATTERNS. Leaving an editor any other way also undoes
  uncommitted changes.

Each generation parameter has a "neutral" value that switches the feature off
(probability 100, scale `cro`, humanize 0, seed `off`, coupling `Ind`). A pattern
with every parameter neutral plays exactly like a plain step sequence.

## 7. GATE editor (the rhythm)

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

![GATE fields Alg, StP, PLS, rot](img/gate-fields-1.svg)

![GATE fields nLn, LEn, Prb](img/gate-fields-2.svg)

The per-step grid for three steps (the green pixel at top right is the gate indicator, dark red = gate off):

![GATE per-step grid](img/gate-grid.svg)

## 8. PITCH editor

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

![PITCH fields Scl, rut, qnt](img/pitch-fields-1.svg)

![PITCH fields Cnt, Ord, p0, p1](img/pitch-fields-2.svg)

The examples show a C# major scale constraint (`Scl`, `rut`), then plain pitches C4 and F#4 (sharp dot at the top right).

## 9. VELOCITY editor

Opens on `Cnt`. The generation fields for humanize, seed and coupling are to the left of it.

| Label | Value | Meaning |
|---|---|---|
| `Hmn` | 0–64 | **Humanization**: each note's velocity varies randomly by up to ± this amount. Results stay between 1 and 127. |
| `Sed` | `off`, 0, 1, 2 ... | **Deterministic seed** for the patterns using this set (see below) |
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
- The seed belongs to the velocity set, so patterns that share a velocity set use
  the same seed number. Each pattern still runs its own copy of the generator.

![VELOCITY fields Hmn, Sed, Cpl](img/velocity-fields-1.svg)

![VELOCITY fields Cnt, Ord, v0](img/velocity-fields-2.svg)

**Coupling** matters when the pitch and velocity sets have different lengths.
With 4 pitches and 3 velocities, `Ind` lets the accents drift across the melody
(a 12-step cycle), while `Lck` ties velocity to the pitch position so the first
pitch is always accented the same way.

## 10. SETTINGS view (F2), label `bPM`

Up/Down changes the tempo by 1 BPM, Shift+Up/Down by 10. Range 40–255. The change
applies immediately and is included in the next save.

![SETTINGS and TEST views](img/settings-test.svg)

## 11. TEST view (F1), label `tst`

A simple value (0–99) adjusted with Up/Down (Shift = ×10). It has no musical
effect; it exists to check the keyboard and display. This is the view GenSeq
starts in.

## 12. Saving and loading

- **Ctrl+S** stores the complete setup: patterns, all sets (also the ones no
  pattern uses), which sets each pattern uses, all generation parameters and the
  tempo. Patterns that shared a set before saving still share it after loading.
  Only committed values are saved, so press Enter in an open editor first.
- Saving does not interrupt playback.
- At power-up GenSeq loads the last save automatically. If nothing was saved it
  starts with defaults silently. If a save exists but cannot be read, the yellow
  banner appears and defaults are used.
- **Saves from earlier firmware** (before shared sets) load normally: every pattern
  gets its own gate, pitch and velocity set, numbered like the pattern (pattern 1
  uses set 0 of each kind, pattern 2 set 1, and so on), and plays exactly as before.
  The next Ctrl+S writes the new format. Firmware from before shared sets cannot
  read that format and starts with defaults, so keep the old firmware file if you
  might go back.

## 13. Quick start

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
7. To hear the melody in a second rhythm: Ctrl+Right to the add slot and Enter. The
   new pattern starts on the same sets, so it plays in unison. Choose its gate stream
   (Left or Up) and press Enter, then **Ctrl+D** to copy the gate set. Go Right twice
   to `PLS`, change it and press Enter. Then Esc and **Shift+Right** switch the pattern
   to the copy (section 5). Both patterns still share the pitch set, so every note you
   change is heard in both rhythms.

## 14. Not in this version

- Muting a single pattern from the keyboard. All patterns play; Delete removes one.
- Per-pattern MIDI channels. All patterns send on channel 1.
- Rhythm generators other than Euclidean.
- Names for sets. Sets are known by their number (0–15) in the set rows.

## Regenerating the screenshots

The pictures in `docs/user/img/` are generated from the firmware's view code by `tools/manual_screens/build.sh`. Re-run it after changing a view.
