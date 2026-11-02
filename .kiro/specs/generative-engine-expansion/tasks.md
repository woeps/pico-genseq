# Implementation Plan: Generative Engine Expansion — Stream Constraints

## Overview

This plan implements the constraints slice of Milestone 3 in emission-lens-first increments. It
begins with the pure, host-testable value object and scale library, then builds the single core1
emission lens that all three shaping controls (scale, probability, humanization) pass through,
proving the default path is byte-identical before any feature is wired. Determinism ownership is
established next, then humanization, scale, and coupling are routed through the lens. Only after
the engine is proven does the plan add the cross-core snapshot/boundary plumbing, the persistence
format v2 with its migration, and the matrix-UI controls (PitchSet scale cue + scale controls,
the GateSet per-step gate+probability grid editor, velocity humanization, and the Pattern-scope
seed/coupling controls). The slice closes only after the host suite is green under sanitizers and
the RP2040 bench reproduces the M2 acceptance gates with Generation Parameters active.

No time-evolving mutation, no new coupling vocabularies beyond independent/locked, no new scales
beyond the extensible chromatic/major/minor minimum, and no MIDI transport or output changes are
part of this plan.

## Tasks

- [x] 1. Introduce the GenerationParameters value object
  - [x] 1.1 Define `common::GenerationParameters` (POD, SDK-free, fixed-size)
    - Add `ScaleId`, `QuantizeMode`, `CouplingMode` enums and the `STEP_PROB_INHERIT` sentinel
    - Fields: scale/root/quantize, patternProbability, stepProbability[kMaxSteps], humanizeRange,
      seedSet/seed, coupling; all defaulting to behavior-preserving values
    - Implement `isDefault()` returning true only when every field is at its default
    - _Requirements: 1.5, 2.3, 3.3, 4.4, 5.3_
  - [x] 1.2 Add host unit tests for the value object
    - Assert a default-constructed value reports `isDefault()`; each single non-default field
      flips it false
    - Assert `STEP_PROB_INHERIT` is distinct from an explicit 100 and from 0
    - New target `example_generation_parameters.cpp`
    - _Requirements: 1.5, 2.3, 3.3_

- [x] 2. Implement the scale model library
  - [x] 2.1 Define `common::scale` (SDK-free free functions)
    - `membershipMask(ScaleId, root)` → 12-bit pitch-class mask; CHROMATIC == 0x0FFF
    - `inScale(mask, note)`; `quantize(mask, mode, note, out)` returning false only for SKIP on an
      out-of-scale note, searching at most 12 semitones for NEAREST/DOWN/UP
    - Define the representation so scales beyond chromatic/major/minor can be added without a
      storage-format bump
    - _Requirements: 1.1, 1.2, 1.3, 1.6_
  - [x] 2.2 Add host unit tests for the scale library
    - Membership for major/minor across all 12 roots; chromatic passes everything
    - Each quantization mode over representative out-of-scale inputs; octave preserved except
      where DOWN/UP must cross an octave; SKIP reports suppression
    - New target `example_scale.cpp`
    - _Requirements: 1.1, 1.2, 1.3, 1.6_

- [x] 3. Build the emission lens with a proven default path
  - [x] 3.1 Add `Sequencer::emitStep(...)` returning an EMITTED/SUPPRESSED status
    - Short-circuit on `isDefault()` to the exact current `noteLifecycle.noteOn(...)` call
    - Fixed evaluation order: probability roll → scale resolve → velocity humanize → note-on
    - _Requirements: 1.1, 2.1, 3.1, 6.1_
  - [x] 3.2 Integrate SUPPRESSED into the RISING-flank lifecycle
    - A SUPPRESSED step emits no note-on and strands no prior note, advancing positions exactly as
      the existing reconcile path does on a missed onset
    - Route both probability-suppression and SKIP-mode suppression through this single path
    - _Requirements: 1.4, 2.2, 6.3_
  - [x] 3.3 Prove default-equivalence
    - Run the existing `property5_midi_stream` and `property2_sequencer_equivalence` harnesses with
      a default `GenerationParameters` installed; assert byte-identical output to pre-milestone
    - _Requirements: 1.5, 2.3, 3.3, 6.1_

- [x] 4. Establish per-Pattern deterministic randomness ownership
  - [x] 4.1 Add a `PatternRng` per Pattern runtime
    - `seed(s)` as a defined function of the seed; `rollPercent()`, `signedRange(r)`, raw `next()`
    - _Requirements: 4.1, 4.2, 4.4_
  - [x] 4.2 Gate RANDOM-order re-routing strictly behind `seedSet`
    - When no seed is set, leave `PitchSet`/`VelocitySet` LCGs untouched (RANDOM output unchanged)
    - When a seed is set, re-route RANDOM-order draws to the Pattern RNG and reseed at the defined
      musical boundary (next bar on Play / seed change)
    - Re-establish generation state consistently after Stop / Timing Fault restart
    - _Requirements: 4.1, 4.2, 4.3, 4.5_
  - [x] 4.3 Add determinism host tests
    - Same seed ⇒ identical trigger/suppress, humanized-velocity, and RANDOM-order sequences across
      repeated runs and across a serialize→deserialize round-trip
    - Unseeded path leaves existing RANDOM-order output byte-identical (regression guard)
    - New target `property8_generation_determinism.cpp`
    - _Requirements: 4.1, 4.2, 4.3, 4.5_

- [x] 5. Route scale, humanization, and coupling through the lens
  - [x] 5.1 Velocity humanization
    - Perturb by `signedRange(humanizeRange)`, clamp to 1..127; zero range == passthrough
    - _Requirements: 3.1, 3.2, 3.3, 3.5_
  - [x] 5.2 Scale constraint at emission
    - Expand `ScaleId`+root into the Pattern runtime's 12-bit mask once on parameter change
    - Apply `scale::quantize`; SKIP returns SUPPRESSED through the §3.2 path
    - _Requirements: 1.1, 1.2, 1.3, 1.4, 6.1_
  - [x] 5.3 Stream coupling read-side map
    - INDEPENDENT advances every stream as today; LOCKED drives the velocity index from the pitch
      position via `pitchSet.getPosition() % velocityCount`, bounded, read-only, no strand
    - _Requirements: 5.1, 5.2, 5.3, 5.4_
  - [x] 5.4 Add constraint/coupling host tests
    - Scale modes incl. SKIP no-strand; humanization bounds; independent==today; locked index map;
      mismatched lengths use the modulo rule
    - New targets `example_emission_lens.cpp`, `example_stream_coupling.cpp`
    - _Requirements: 1.1–1.5, 3.1–3.3, 5.1–5.4_

- [x] 6. Cross-core handoff and boundary application of Generation Parameters
  - [x] 6.1 Extend `sequencer::PatternSnapshot` with a `GenerationParameters` member
    - Keep the POD, fixed-capacity, heap-free discipline; update `footprintBytes()`
    - _Requirements: 6.1, 6.2_
  - [x] 6.2 Add the command + pending-operation path for parameter edits
    - Apply probability/humanize/scale at next beat, coupling/seed at next bar, via the M2
      command/snapshot boundary mechanism; no tearing, no stranded notes
    - _Requirements: 6.2, 10.5_
  - [x] 6.3 Add allocation and boundary host tests
    - Extend `example_steady_state_allocation` to prove zero Hot Path allocation under every
      parameter combination
    - Boundary-apply tests immediately before/on/after beat and bar boundaries
    - _Requirements: 6.1, 6.2, 6.3_

- [x] 7. Persistence format v2 and pre-milestone migration
  - [x] 7.1 Bump `FORMAT_VERSION` to 2 and append the Generation Parameters block
    - Serialize scale/root/quantize, patternProbability, humanizeRange, seedSet/seed, coupling,
      count-driven stepProbability, and the explicit bit-packed gate array (new to the format —
      manual gates can diverge from the generator seed)
    - _Requirements: 6.5, 1.7, 2.6, 5.5, 9.8_
  - [x] 7.2 Implement the migration decoder
    - version==2 decodes the appended block; version==1 decodes the v1 payload and sets all
      Generation Parameters to defaults (and derives the gate array from the stored generator
      params, as today); version>2/unknown stays BAD_VERSION
    - Preserve CRC, local-decode-then-assign, and the ReadCursor bounds guard
    - _Requirements: 6.5_
  - [x] 7.3 Add persistence host tests
    - v2 round-trips all Generation Parameters and the explicit gate array; a hand-built v1 record
      loads with defaults; truncation/CRC guards still hold; `out` untouched on failure
    - Extend the persistence test suite (`test/persistence/`), e.g. `property7_genparams_roundtrip.cpp`
    - _Requirements: 6.5, 1.7, 9.8_

- [x] 8. PitchSet view — scale membership cue and scale controls
  - [x] 8.1 Add the read-only scale-membership cue (R8)
    - When a pitch position is selected and scale != CHROMATIC, light a reserved indicator pixel
      distinct from the dirty-draft pixel at `(15,0)`, computed via the same `common::scale`
      library used at emission; render byte-identical when CHROMATIC; never touch draft/commit/undo
    - _Requirements: 8.1, 8.2, 8.3, 8.4, 8.5, 8.6_
  - [x] 8.2 Add SCALE / ROOT / QUANTIZE control fields (R10)
    - New PitchSet fields before the pitch fields; adjust cycles the enums; clear-to-default sets
      CHROMATIC; applies via the boundary path; action count ≤2 per field (stated)
    - _Requirements: 10.1, 10.2, 10.3, 10.4, 10.5_
  - [x] 8.3 Add PitchSet view host tests
    - Cue matches `scale::inScale` and is absent when chromatic; scale/root/quantize fields set and
      clear; no change to draft/commit behavior
    - New target `example_pitchset_scale_view.cpp`
    - _Requirements: 8.1–8.6, 10.1–10.5_

- [x] 9. GateSet view — per-step gate and probability grid editor
  - [x] 9.1 Give the GateSet draft an explicit per-step gate array
    - The generator (`createEuclidean`) writes gate states into the draft array rather than the
      array being derived only on send; manual edits layer on top
    - _Requirements: 9.2, 9.3, 9.7_
  - [x] 9.2 Add the per-step grid to the field cursor
    - Extend the GateSet field model with one field per step after the generator fields; grid
      length tracks `STEPS`; `moveGateSetProperty` walks generator then grid
    - _Requirements: 9.1_
  - [x] 9.3 Edit gate and probability per step
    - UP/DOWN cycles that step's probability (INHERIT → 0% → … → 100%, INHERIT distinct); ENTER on
      a step toggles its gate on/off
    - _Requirements: 9.2, 9.4, 9.5_
  - [x] 9.4 Define the generator re-apply policy
    - Re-touching a generator property regenerates gate states but preserves per-step probability
      values; new steps default to INHERIT on growth; out-of-range entries inert on shrink
    - _Requirements: 9.3_
  - [x] 9.5 Render the grid and add the per-Pattern PROBABILITY field (R10)
    - Step row with lit/unlit gates, edited step highlighted, probability as a coarse level bar,
      INHERIT distinct; add the per-Pattern probability field with clear-to-100 (action count ≤2)
    - _Requirements: 9.6, 10.1, 10.2, 10.3, 10.4_
  - [x] 9.6 Add GateSet view host tests
    - Grid toggles gates and cycles probability; generator reseeds gates while preserving
      probability; generator-only output byte-identical to today's `createEuclidean`; edits commit
      via the draft path and survive save/load
    - New target `example_gateset_grid_view.cpp`
    - _Requirements: 9.1–9.8, 10.1–10.5_

- [ ] 10. VelocitySet humanize field and Pattern-scope seed/coupling controls
  - [x] 10.1 Add the HUMANIZE field to the VelocitySet view
    - Adjust 0..N; clear-to-0; applies via the boundary path; action count ≤2
    - _Requirements: 10.1, 10.2, 10.3, 10.4, 10.5_
  - [x] 10.2 Add Pattern-scope SEED and COUPLING fields
    - A Pattern/Settings field for the seed with an explicit "no seed" clear state, and a COUPLING
      field cycling INDEPENDENT/LOCKED with clear-to-INDEPENDENT
    - (Hosted in the VelocitySet view — the existing per-pattern editor with the move/adjust
      vocabulary — rather than a separate Settings view, since seed/coupling are per-Pattern.)
    - _Requirements: 10.1, 10.2, 10.3, 10.4, 10.5_
  - [x] 10.3 Verify the full R10 control map
    - Assert every Generation Parameter has a demonstrated matrix-UI set-and-clear path with a
      bounded, stated action count; no parameter is tooling-only
    - _Requirements: 10.1, 10.6_
  - [x] 10.4 Wire the GEN_PARAMS_SET FIFO transport end-to-end (deferred from tasks 6/8/9/10)
    - Add `commands::sendGenParams(patternIndex, GenerationParameters)` + FIFO wire encode, and
      decode it into the `PatternSnapshot` gen-params in `receiveCommandInto`, so every UI control
      built in tasks 8-10 reaches core1 through the real cross-core path (not just the direct
      `setPatternGenerationParameters` setter the UI/tests use meanwhile)
    - Route each view's gen-param edit through `sendGenParams` at the command/boundary point
    - Host test the encode→decode→apply round-trip; one consolidated transport change verified on
      silicon in 11.3
    - _Requirements: 6.2, 10.1, 10.5_

- [ ] 11. Host suite and RP2040 bench acceptance gate
  - [ ] 11.1 Green the full host suite under sanitizers
    - All new and existing CTest targets pass under ASan/UBSan; default-equivalence and
      determinism guards included
    - _Requirements: 6.1, 6.3, 1.5, 2.3, 3.3, 4.3_
  - [ ] 11.2 Prove no M2 regression on silicon
    - With Generation Parameters active, re-run the M2 acceptance gates (save transparency, UI-loop
      bound, stuck-note count) and confirm unchanged timing vs the M2 soak under worst-case load
    - _Requirements: 6.1, 6.3, 6.4_
  - [ ] 11.3 Prove per-feature live control and determinism on silicon
    - Each parameter toggled via its R10 control applies at the boundary with no stuck note and
      within the UI responsiveness bound; a seeded Pattern reproduces the same decoded MIDI across a
      power-cycle + load
    - Store commands, seeds, firmware revision, analyzer setup, and captures as milestone evidence
    - _Requirements: 4.3, 6.2, 10.5, 10.6_

## Open hardware issue (blocks 11.2/11.3) — core1 fails to launch at boot

On the RP2040 bench, the M3 firmware boots core0 fully (UART prints through
"Entering Main View", so UIController::initialize + the FRAM load + the
restoreSequencerFromState sync all run), but **core1 never starts
sequencer_task** — SWD shows core1 parked in the bootrom (PC 0x12a) and core0
then wedged in `multicore_fifo_push_blocking` (`multicore_fifo_wready`,
multicore.h:157) because nothing drains the inter-core FIFO. OpenOCD reports the
core1 state as "double fault / lockup". A HardFault handler installed to capture
the stacked frame never ran, consistent with core1 not executing app code at all
(fault during exception entry / never-launched → lockup, not a handled fault).

Evidence gathered (this session):
- core0 boot UART: full normal boot to Main View.
- core0 PC at hang: multicore_fifo_wready (blocked push).
- core1 PC: 0x0000012a (bootrom); a hw breakpoint on sequencer_task was never hit.
- This is NOT the v1 gate-seeding bug (fixed this session in PersistMapping) and
  NOT a PatternSnapshot stack overflow (commandSnapshot is static). The boot
  order in main() and core1's apply path are byte-identical to M2.

Leading hypotheses to test next (deliberately, with the probe attached):
1. core1 launch-handshake vs command FIFO collision — `multicore_launch_core1`
   uses the SIO FIFO for its boot handshake, and the app reuses that same FIFO
   for commands:: messages. If any FIFO traffic races the launch handshake the
   launch can fail. Test: confirm core1 reaches sequencer_task's first
   instruction before any sendCommand runs; add a launch barrier if needed.
2. core1 faults immediately in sequencer_task static init / first processCommand
   on the enlarged Pattern/Sequencer (GenerationParameters per Pattern,
   std::vector<PatternRng>). Test: launch with multicore_launch_core1_with_stack
   and an explicit larger stack; breakpoint-step sequencer_task from entry.

The entire M3 engine + UI is host-proven (41/41, incl. the real-command.cpp
GEN_PARAMS_SET FIFO round-trip). This is a hardware-only bring-up defect to pin
in a focused bench session, as task 11.2.

### Update (sharpened, reproducible): core1 faults applying a boot-sync command

Later session findings refine the above. The core0 host-control RX silence was a
SEPARATE, now-FIXED bug: UIState grew to ~1.4 KB and was copied by value in the
reducer/handleEvent path, overflowing core0's 2 KB stack — fixed by reducing
UIState in place (commit "reduce UIState in place", host 41/41).

The remaining HARDWARE defect is now deterministic and isolated:
- M2 (ff900c9) boots clean (LED on). Flashing the latest M3 build over the SAME
  FRAM record → boot fails, LED dark. Reproducible, so it is an M3 code bug, not
  flakiness.
- core0 wedges at multicore_fifo_wready (blocked pushing a boot-sync command).
- core1 LAUNCHES then faults (PC in a fault/lockup handler, NOT bootrom), i.e. it
  crashes APPLYING a command from restoreSequencerFromState's sync burst.
- The FRAM record is read CORRECTLY: latest's v1 decode order matches M2's v1
  serialize byte-for-byte, and the host property7 v1-migration test passes. So the
  loaded PersistableConfig is valid; the fault is downstream on core1's apply path.

Isolation target (bisect deliberately, probe attached, board known-good on M2
first): the fault is in core1's enlarged Sequencer / command-apply path
(per-pattern GenerationParameters, std::vector<PatternRng>, the task-10.4
GEN_PARAMS_SET handling, or expandGateStates' interaction with the sync burst).
Suggested bisection: build M3 with emitStep/PatternRng/GEN_PARAMS_SET reverted to
confirm core1 boots, then re-add each to find the trigger. Also test core1 launch
with multicore_launch_core1_with_stack + a larger explicit stack, since the
core0 overflow proved the 2 KB default is marginal for the enlarged structures.

The entire M3 engine + UI + the GEN_PARAMS_SET transport remain host-proven
(41/41). This is a hardware bring-up defect to pin in a focused bench session.
