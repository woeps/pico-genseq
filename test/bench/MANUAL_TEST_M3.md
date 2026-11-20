# M3 manual test script: Generation Parameters (generative-engine-expansion)

Firmware: default build (no host-control, no traces) of `7846c38` or later.
Setup: USB keyboard on the GenSeq, MIDI out into a synth or a MIDI monitor
(for example `aseqdump -p <port>` through a USB-MIDI interface). A monitor is
the easiest way to read pitches and velocities exactly.

## Key map

| Key | Where | Action |
|---|---|---|
| F3 | anywhere | PATTERNS view |
| F2 | anywhere | SETTINGS (BPM: Up/Down ±1, Shift ±10) |
| Space | anywhere | Play / Stop |
| Ctrl+S | anywhere | Save to FRAM (save banner shows) |
| Ctrl+Left/Right | PATTERNS | select pattern (rightmost slot = "add") |
| Up / Down / Left / Right | PATTERNS | select set: Up = GATE, Right = PITCH, Down = VELOCITY (from GATE) |
| Enter | PATTERNS | open the selected set's editor (on the add slot: add a pattern) |
| Delete | PATTERNS | delete the selected pattern |
| Left / Right | editor | move between fields |
| Up / Down (Shift = coarse) | editor | change the field |
| Enter | editor | **commit** (every change is audible immediately as a preview; Enter keeps it) |
| Esc | editor | undo the uncommitted draft; Esc again leaves to PATTERNS |

Display: yellow label at the top names the field; the value (number, note or text
such as `maj`, `off`, `inh`) is centred below it. Pixel (15,0)
green = uncommitted draft. To reach a set from anywhere: `F3`, `Left`, `Up`
selects GATE; then `Right` = PITCH or `Down` = VELOCITY.

## 0. Starting state

The board boots with the config the last bench run saved: **1 pattern, 200 BPM,
GATE Prb 50, VELOCITY Hmn 20, Sed 7.** Step 1 tests clearing those back to defaults.

- [ ] Boots normally (display/LED alive, keyboard responds). Space plays notes.

## 1. Clear-to-default (R10.3)

1. VELOCITY editor (`F3 Left Up Down Enter`). Opens on `Cnt`.
2. `Left` x3 -> `Hmn 20`. Hold `Down` to `0`.
3. `Right` -> `Sed 7`. `Down` x8 -> value shows `off` = **no seed**.
4. `Right` -> `Cpl Ind` (leave it). `Enter` to commit, `Esc`.
5. GATE editor (`F3 Left Up Enter`). Opens on `Alg euc`. `Right` x6 -> `Prb 50`. `Shift+Up` x5 -> `100`. `Enter`, `Esc`.
6. F2: set BPM to 120.

- [ ] While playing: 4 notes per bar on every 4th sixteenth, pitches C4 G4 A4 C5 repeating, velocities 100 80 60 80 repeating. This is the pre-M3 behaviour.

## 2. Step probability, per pattern (R2)

1. GATE editor -> `Prb`. While playing, press `Down` a few times.
- [ ] No extra note fires on each key press (fixed 2026-11-20), and the thinning is audible
  **before** Enter. `Esc` returns to 100 audibly. Set `50`, `Enter`.
- [ ] Playing: about half the onsets are missing, irregularly. No hanging notes.
2. Set `0`, `Enter`. - [ ] Silence (clock keeps running).
3. Set `100`, `Enter`. - [ ] Every onset sounds again.

## 3. Per-step gate + probability grid (R9)

1. GATE editor, `Right` x7 -> `s0`: gate pixel (13,0) **green** (step 0 is on), value `inh` = INHERIT.
2. `Right` -> `s1`: gate pixel dim red (off). `Enter` toggles it -> green.
3. Still on `s1`: `Up` -> `0`, `Up` again -> `10` ... -> `100`; `Down` below `0` returns to `inh`. Leave `s1` at `100`.
4. Go to `s0` (`Left`), `Up` -> `0` (step 0 never sounds).
5. `Left` back to `Prb` (Enter on the grid toggles, so commit from a non-grid field), `Enter`.

- [ ] Step 1 (the second sixteenth) now plays every bar; step 0 never plays; steps 4, 8, 12 still play.
6. Generator reseed keeps probabilities: go to `PLS` (`Left` x4 from Prb), `Up` once (5 pulses).
- [ ] Gates are regenerated (Euclidean 5/16), but walking the grid shows `s0` still at `0` and `s1` at `100`. `Esc` to discard, or `Enter` to keep.
7. Clean up: `Esc` (undo), or set `s0`/`s1` back to INHERIT, Pulses 4, commit.

## 4. Scale constraint + quantize + cue (R1, R8)

1. PITCH editor (`F3 Left Up Right Enter`). Opens on `Cnt`. `Right` -> order, `Right` -> `p0 C4`.
2. Change p1 (G4) to `G#4`: `Right`, `Up` once. `Enter` (commit), keep the editor open.
3. `Left` x6 to `Scl cro` (field 0). `Up` -> `Scl maj`. `Right` -> `rut c`. `Right` -> `qnt nea`. `Enter`.
4. Walk right to the pitch fields:
- [ ] `p0 C4` cue pixel (14,0) **green**; `p1 G#4` **red**; `p2 A4` green; `p3 C5` green.
- [ ] Playing: G#4 comes out as **G4 or A4** (NEAREST), never G#4.
5. Back to `qnt`: `Up` -> `dwn`, `Enter`. - [ ] G#4 plays as **G4**.
6. `Up` -> `up`, `Enter`. - [ ] G#4 plays as **A4**.
7. `Up` -> `skp`, `Enter`. - [ ] That step is **silent**; the other three still play; no hanging note.
8. `rut`: `Up` until `a ` (A), `Scl` `Up` -> `min` (A minor = white keys), `qnt nea`, `Enter`.
- [ ] Cue: C4 / A4 / C5 green, G#4 red.
9. Clear: `Scl` `Down` -> `cro`, `Enter`. - [ ] No cue pixel on any pitch; G#4 plays as G#4. Set p1 back to G4 if you like.

## 5. Velocity humanization (R3)

1. VELOCITY editor -> `Hmn`. `Shift+Up` x2 -> `20`. `Enter`.
- [ ] Monitor: velocities jitter within ±20 of 100/80/60/80 (so 80..120, clamped at 127, etc.). Never 0.
2. `Hmn 0`, `Enter`. - [ ] Exactly 100 80 60 80 again.

## 6. Deterministic seed (R4)

1. Set GATE `Prb 50` and VELOCITY `Hmn 20` (commit both). PITCH order `Rnd` (field `Ord Fwd` -> `Up` x3 -> `Ord Rnd`), commit.
2. VELOCITY `Sed`: `Up` from no-seed -> `0`, `Up` x7 -> `7`. `Enter`.
3. Play ~2 bars, Stop. Play again, Stop. Compare the monitor output.
- [ ] Both runs: **identical** sequence of which steps sound, their pitches, and velocities.
4. `Sed 8`, commit, play. - [ ] A different sequence from seed 7.
5. Back to `Sed 7`, commit. `Ctrl+S` (save banner). Unplug USB power, plug back in, Play.
- [ ] Identical to the seed-7 runs in step 3 (this is the bug fixed in 7fa3b7d).
6. `Sed` -> `Down` below 0 (no seed), commit. Play twice. - [ ] Runs now differ from each other.

## 7. Stream coupling (R5)

1. Clear seed/humanize/probability (as in section 1). VELOCITY `Cnt` -> `3` (velocities 100 80 60) so the lengths differ from the 4 pitches. Commit.
2. `Cpl Ind` (default). - [ ] Pitch/velocity pairs drift: C4 gets 100, then 80, then 60...
3. `Cpl` `Up` -> `Lck`, `Enter`.
- [ ] Each pitch always gets the same velocity: C4 = 100, G4 = 80, A4 = 60, C5 = 100 (pitch position mod 3).
4. `Cpl Ind`, commit. - [ ] Drift returns.

## 8. Live edits while playing (R10.5)

While playing at 200+ BPM, change each field above and commit.
- [ ] Each change takes effect within a bar, the UI keeps responding, no stuck note after Stop.

## 9. Persistence (format v2)

1. Set a mix: Prb 70, s1 = 100, Scl maj / rut d / qnt dwn, Hmn 10, Sed 3, Cpl Lck. Commit each. `Ctrl+S`.
2. Unplug power, plug back in.
- [ ] Every field reads back as set (walk each editor), and playback matches what it was before the power-cycle.

## 10. Save under load (M2 regression)

1. Add patterns (F3, `Ctrl+Right` to the add slot, `Enter`) up to 3 or more, BPM 255, Play.
2. Press `Ctrl+S` repeatedly while playing.
- [ ] No audible hiccup in timing, no stuck notes, save banner each time.
