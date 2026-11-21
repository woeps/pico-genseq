# Manual test script: shared gate / pitch / velocity sets

Covers everything added or changed by the `separate-velocity-set-gate-set-and-note-set`
feature: set libraries, the set rows on the display, slot navigation, new / copy / delete,
sharing and switching sets, the go-to picker, saving, the never-seeded gate fix and the
firmware metadata. The user manual (`docs/user/user-manual.md`, sections 2, 4, 5 and 12)
describes the same features for end users.

Firmware: default build (no host-control, no traces) of the PR head.
Setup: USB keyboard on the GenSeq, MIDI out into a synth or a MIDI monitor (for example
`aseqdump -p <port>` through a USB-MIDI interface). A monitor makes it easy to see which
notes and velocities each pattern plays. Everything plays on MIDI channel 1, so two patterns
playing the same note show up as two Note Ons.

## Reading the display

- **Editors (GATE, PITCH, VELOCITY):** row 15 (bottom) is the **set map**, one pixel per set,
  set 0 at the left. Dim green = the set exists, white = the set you are editing. Row 14 is
  **users**: one red pixel per pattern that uses this set, pattern 1 at the left.
- **PATTERNS:** rows 13, 14 and 15 are the gate, pitch and velocity set maps of the selected
  pattern. White = the set this pattern uses, dim green = other sets that exist.
- A refused set action shows a word in the value area (`use`, `ful`, `unu`) until the next key.
- Pixel (15,0) red = uncommitted change in this editor (unchanged from before).

To reach a stream from anywhere: `F3`, then `Left`, `Up` selects GATE; from GATE `Right` =
PITCH, `Down` = VELOCITY. `Ctrl+Left` x15 selects pattern 1.

## 0. Starting state

The board was left at factory defaults and saved: **1 pattern, 120 BPM, one default set in
each library.**

1. Power-cycle the board. Press `F3`.
- [ ] One red square (pattern 1) and the yellow add slot.
- [ ] Rows 13, 14 and 15 each show a single **white** pixel at the far left (pattern 1 uses
  gate set 0, pitch set 0, velocity set 0) and nothing else.
2. `Space`.
- [ ] Notes play: 4 per bar, pitches C4 G4 A4 C5 repeating, velocities 100 80 60 80. (Before the
  never-seeded gate fix, a saved, never-edited default pattern played no notes after a reboot.)

## 1. A new pattern shares the existing sets

1. In PATTERNS, `Ctrl+Right` to the add slot, `Enter`.
- [ ] A second red square appears and is selected. It plays **in unison** with pattern 1 (each
  note now arrives twice on the monitor). It is audible right away, without opening its gate
  editor (never-seeded gate fix: before, a new pattern was silent).
- [ ] Rows 13 to 15 for pattern 2 again show one white pixel at the left: it uses the same sets.
2. `Left`, `Up` (GATE), `Enter`.
- [ ] Gate editor opens on `Alg euc`. Row 15: one white pixel at x=0. Row 14: **two** red pixels
  (x=0 and x=1): the set is shared by patterns 1 and 2. Pixel (15,0) is **not** red.

## 2. Editing a shared set changes every user

1. Gate editor (still on set 0): `Right` x2 to `PLS`, `Up` twice (6 pulses).
- [ ] Both patterns change rhythm **together** while you press (heard before Enter), without a
  hiccup in the clock. Pixel (15,0) red.
2. `Down` twice back to 4.
- [ ] Pixel (15,0) goes dark again (edited back to the committed value).
3. `Up` twice again, `Enter`.
- [ ] Both patterns keep 6 hits per bar.

## 3. Copy a set and switch one pattern to it (one melody, two rhythms)

1. Still in the gate editor: `Ctrl+D`.
- [ ] The label returns to `Alg`. Row 15: x=0 dim green, **x=1 white** (the copy). Row 14 is
  **empty**: no pattern uses the copy yet.
2. `Right` x2 to `PLS`, `Down` x4 (2 pulses), `Enter`.
- [ ] Nothing changes in what you hear: an unused set is not played.
3. `Esc` (back to PATTERNS, pattern 2 and GATE still selected). `Shift+Right`.
- [ ] Row 13 for pattern 2: the white pixel moves to **x=1**, x=0 is dim green.
- [ ] Pattern 2 now plays 2 hits per bar, pattern 1 keeps 6. Both keep the same pitches and
  velocities. The switch happens without restarting the bar.
4. `Shift+Right` again.
- [ ] It wraps back to set 0 (white at x=0): pattern 2 is in unison with pattern 1 again.
  `Shift+Left` back to set 1.

## 4. Shared pitch set: one edit, both rhythms

1. In PATTERNS (pattern 2), `Right` (PITCH), `Enter`.
- [ ] Pitch editor opens on `Cnt 004`. Row 14: two red pixels (shared by both patterns).
2. `Right` x2 to `p0` (`c 4`), `Up` x2 (`d 4`).
- [ ] Both patterns play D4 instead of C4, each in its own rhythm. `Enter`.
3. `Left` to `Ord`, `Up` x3 (`Rnd`), `Enter`.
- [ ] Both patterns pick notes at random, **independently**: on beats where both play, they
  often play different notes. Set `Ord` back to `Fwd` (`Down` x3) and `Enter`.
- [ ] With `Fwd`: pattern 2 (2 hits per bar) walks through the 4 pitches more slowly than
  pattern 1 (6 hits per bar). Each pattern keeps its own position in the shared set.

## 5. Slot navigation and the uncommitted change

1. Open the gate editor of pattern 2 (`F3`, `Left`, `Up`, `Enter`): it opens on set 1 (row 15:
   x=1 white). Row 14: one red pixel at **x=1** (only pattern 2).
2. `Right` x2 to `PLS`, `Up` (3 hits; pattern 2 changes at once). Pixel (15,0) red.
3. `Ctrl+Right`.
- [ ] The editor switches to set 0 (row 15 x=0 white, row 14 shows pattern 1 only, x=0 red).
  The uncommitted change to set 1 is **undone**: pattern 2 is back to 2 hits.
4. `Ctrl+Right` again.
- [ ] Wraps back to set 1. `Ctrl+Left` goes to set 0, `Ctrl+Left` again to set 1.

## 6. New, delete and the refusals

1. In the gate editor on set 1: `Ctrl+Delete`.
- [ ] Value area shows `use` (pattern 2 uses it). Nothing is deleted. The next key press
  clears the word.
2. `Ctrl+N`.
- [ ] Label `Alg`; row 15: x=0 and x=1 dim green, **x=2 white**; row 14 empty.
3. `Ctrl+Delete`.
- [ ] Set 2 is gone (row 15 shows only x=0 and x=1) and the editor moved to another set.
4. Velocity editor (`F3`, `Left`, `Up`, `Down`, `Enter`), only one velocity set exists:
   `Ctrl+Delete`.
- [ ] `use`: the last set always belongs to a pattern, so it can never be deleted.
5. Optional, library full: in any editor press `Ctrl+N` until row 15 is full (16 pixels),
   then once more.
- [ ] `ful`, nothing changes. Clean up with `Ctrl+Delete` on each new set (they are unused).

## 7. Go to a pattern (Ctrl+G)

1. Pitch editor of pattern 2 (`F3`, `Left`, `Up`, `Right`, `Enter`), set 0 shared by both.
   `Ctrl+G`.
- [ ] Picker: label `gto`, value **`002`** (it starts on the pattern you came from). Row 14:
  x=0 dim purple, x=1 white. Row 15 (the set map) is hidden while the picker is open.
2. `Right`.
- [ ] Value `001`, white moves to x=0. `Right` again wraps to `002`. `Up` / `Down` move too.
3. While the picker is open, editing keys do nothing: `Shift+Up`, `Ctrl+N` and `Ctrl+Delete`
   change nothing. `Space` still starts / stops playback.
4. `Esc`.
- [ ] Back in the pitch editor, same set, same field, any uncommitted change still there.
5. `Right` x2 to `p0`, `Up` (uncommitted change, pixel (15,0) red). `Ctrl+G`, `Right` to
   `001`, `Enter`.
- [ ] PATTERNS view with **pattern 1** selected. The uncommitted pitch change is undone (heard).
6. `Ctrl+Right` (select pattern 2), `Left`, `Up`, `Enter`: gate editor on set 1, used only by
   pattern 2. `Ctrl+G`.
- [ ] No picker: jumps straight to PATTERNS with pattern 2 selected.
7. Any editor: `Ctrl+N`, then `Ctrl+G`.
- [ ] `unu` (no pattern uses the new set). `Ctrl+Delete` to remove it again.

## 8. Save and reload keep the sharing

Starting point: 2 patterns, both on pitch set 0 and velocity set 0, pattern 1 on gate set 0
(6 hits), pattern 2 on gate set 1 (2 hits).

1. Optional: create an unused set to check it survives (velocity editor: `Ctrl+N`, `Right` x2
   to `v0`, `Up`, `Enter`, `Esc`).
2. `Ctrl+S`.
- [ ] Green save banner; playback keeps running without a gap.
3. Power-cycle the board. `F3`, select each pattern.
- [ ] Same 2 patterns, same set rows: pattern 1 white at gate x=0 / pitch x=0 / velocity x=0,
  pattern 2 white at gate **x=1** / pitch x=0 / velocity x=0. The unused velocity set (if you
  made one) shows as a dim green pixel.
- [ ] `Space`: the same two rhythms over the same melody. Pitch editor row 14 still shows two
  red pixels.

## 9. Deleting a pattern keeps its sets

1. In PATTERNS select pattern 2, `Delete`.
- [ ] One pattern left. Its row 13 still shows gate set 1 as a dim green pixel at x=1.
2. Gate editor, `Ctrl+Right` to set 1.
- [ ] Row 14 empty (no user any more). `Ctrl+Delete` now succeeds.

## 10. Editing a big shared gate set stays smooth

1. Add patterns until there are 4 or more (`Ctrl+Right` to the add slot, `Enter`, repeat). All
   new patterns share gate set 0.
2. Gate editor on set 0: `Right` to `StP`, `Shift+Up` until `064`; `Right` x3 to `nLn`, `Up`
   until `001` (whole notes); `Enter`. `F2`, set BPM to 255 (`Shift+Up` until it stops).
3. Playing, open the gate editor and **hold** `Up` / `Down` on `PLS` for a few seconds.
- [ ] No stutter in the clock or notes, no stuck notes after `Space` (Stop), the keyboard keeps
  responding. (On the bench, 15 patterns sharing this set measured at most 8.9 ms UI gap and
  0 missed ticks.)

## 11. Firmware metadata (host side)

1. On the PC, after building in `build/`: `picotool info -a build/genseq.uf2` (or on the
   device in BOOTSEL mode).
- [ ] Program version `0.1+<git hash>`, the feature list (shared sets, FRAM save format v3,
  ...), the pinout (GPIO 8/9 MIDI, 10-13 FRAM, 16 LED matrix, 25 status LED) and the
  `git <hash>` build attribute.

## Not covered here

- Loading a save made by firmware from before shared sets: verified on the bench (task 10.1,
  identical MIDI). Repeat it by flashing an older build, saving, then flashing this one.
- Cross-core timing numbers: see `tasks.md` 10.2 in the spec folder.
