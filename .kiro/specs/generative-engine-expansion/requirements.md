# Requirements Document: Generative Engine Expansion — Stream Constraints

## Introduction

This milestone begins GenSeq's evolution from a set of independent, unconstrained generative
streams into a coherent, steerable generative engine. Today each `common::Pattern` owns three
streams that advance independently: a `PitchSet` (ordered pitch bytes plus a `PlayingOrder`), a
`VelocitySet` (ordered velocity bytes plus a `PlayingOrder`), and a `GateSet` (a boolean rhythm,
optionally produced by a Euclidean generator). Pitch and velocity traversal supports FORWARDS,
BACKWARDS, PENDULUM, and RANDOM order; RANDOM draws from a per-stream linear congruential
generator. There is no musical constraint on which pitches may be emitted, no probabilistic
shaping of whether a gated step actually sounds, no velocity humanization, and no coupling that
keeps the three streams musically related to one another.

This first slice of the milestone adds **constraint and shaping controls** to the existing
streams without introducing time-evolving mutation (which is deferred to a later slice). It lets
a performer bound randomness so generated material stays musical and recognizable: pitch output
can be constrained to a scale/key, step triggering can be made probabilistic, velocity can be
humanized within bounds, and the three streams can optionally be coupled so they share a musical
phrase rather than drifting apart. Every control is deterministic when the performer requests
determinism, legible on the LED matrix, and recoverable (no stuck notes, no torn state) under
the performance-safe invariants established in Milestone 2.

Every generative control added in this slice MUST be controllable from the matrix UI in a
simple way — settable, adjustable, and clearable — even though the richer interaction-design
work belongs to a later milestone. A feature whose value can only be reached through
host-control, serial, or persistence tooling is not considered shippable in this milestone. This
slice establishes the *minimum viable* simple control for each parameter, not its final
ergonomics.

This slice does NOT add generative mutation or variation-over-time operators, does not add scenes
or arrangement, does not change the MIDI transport or output topology, does not add MIDI CC
generation, and does not design the final hardware controls. Those belong to later slices and
milestones. All additions MUST preserve the Milestone 2 real-time guarantees: bounded,
allocation-free hot-path behavior on core1, transparent saving, correct note lifecycle, and
fail-closed recovery.

## Glossary

- **Stream**: One of the three per-Pattern generative sources — the PitchSet, the VelocitySet,
  or the GateSet.
- **PitchSet / VelocitySet / GateSet**: The existing `common::PitchSet`, `common::VelocitySet`,
  and `common::GateSet` classes, unchanged in their traversal roles.
- **PlayingOrder**: The existing `common::PlayingOrder` enum (FORWARDS, BACKWARDS, PENDULUM,
  RANDOM) governing pitch and velocity traversal.
- **Scale Constraint**: A musical rule set (a root pitch class plus a set of allowed scale
  degrees) that maps an arbitrary candidate pitch onto an allowed pitch.
- **Quantization Mode**: The policy used to resolve an out-of-scale candidate pitch to an
  in-scale pitch (e.g. snap to nearest, snap down, snap up, or skip).
- **Step Probability**: A per-Pattern or per-step likelihood that a gated step actually produces
  a note-on, expressed as an integer percentage.
- **Velocity Humanization**: A bounded, deterministic-when-seeded perturbation applied to a
  stream's emitted velocity, expressed as a symmetric range around the stored velocity.
- **Deterministic Seed**: An explicit per-Pattern seed value that makes all probabilistic and
  humanized behavior reproducible across runs and across a save/load round-trip.
- **Stream Coupling**: An optional mode in which the VelocitySet and/or GateSet advance in a
  defined relationship to the PitchSet rather than on independent positions, so the three
  streams express one coordinated phrase.
- **Generation Parameters**: The complete set of constraint and shaping values owned by a
  Pattern (scale constraint, quantization mode, step probability, humanization range, seed,
  coupling mode).
- **Working State**: Accepted musical configuration eligible for explicit save, as defined in
  Milestone 2. Generation Parameters are part of Working State.
- **Legible**: Representable to the performer through the LED matrix without text-heavy menus.
- **Simple Control**: A matrix-UI path to set, adjust, and clear a parameter that reuses the
  established view interaction vocabulary (move-property / adjust-value / ENTER-commit /
  ESCAPE-undo, or the pattern-select model) and requires only a small, stated number of discrete
  actions — as opposed to a host-control command, serial console, or text-heavy modal menu.
- **Hot Path**: The core1 per-tick sequencing path that must remain bounded and allocation-free,
  as established in Milestone 2.

## Requirements

### Requirement 1: Scale and key constraint for pitch output

**User Story:** As a performer, I want generated pitches constrained to a chosen scale and key,
so that randomized or evolving pitch material stays musical.

#### Acceptance Criteria

1. WHEN a performer assigns a Scale Constraint (root pitch class plus allowed scale degrees) to a Pattern, THE Sequencer SHALL map every emitted pitch of that Pattern onto an allowed pitch before producing the note-on.
2. WHILE a Scale Constraint is active, THE Sequencer SHALL preserve the octave of the candidate pitch except where the selected Quantization Mode requires moving to an adjacent octave to find an allowed pitch.
3. WHEN a candidate pitch is not in the active Scale Constraint, THE Sequencer SHALL resolve it according to the Pattern's Quantization Mode (snap to nearest allowed, snap down, snap up, or skip the step).
4. WHEN the Quantization Mode is "skip" and the candidate pitch is out of scale, THE Sequencer SHALL suppress the note-on for that step without stranding any previously sounding note.
5. WHEN no Scale Constraint is assigned, THE Sequencer SHALL emit the stored PitchSet pitches unchanged, preserving current behavior exactly.
6. THE Scale Constraint SHALL support at minimum the chromatic (unconstrained), major, and natural minor scales, and THE design SHALL define the representation so additional scales can be added without changing the storage format version unnecessarily.
7. THE Scale Constraint and Quantization Mode SHALL be part of Working State and SHALL survive an explicit save and boot-time load round-trip unchanged.

### Requirement 2: Probabilistic step triggering

**User Story:** As a performer, I want some gated steps to sound only part of the time, so that a
repeating pattern breathes and varies without becoming unrecognizable.

#### Acceptance Criteria

1. WHEN a Pattern has a Step Probability below 100 percent, THE Sequencer SHALL, on each gated step, produce the note-on only with that probability and otherwise suppress it.
2. WHEN a step is suppressed by Step Probability, THE Sequencer SHALL NOT advance any note lifecycle in a way that leaves a note stuck, and SHALL still advance stream positions consistently with the unsuppressed case so the Pattern's phrase length is unchanged.
3. WHEN Step Probability is 100 percent, THE Sequencer SHALL trigger every gated step, preserving current behavior exactly.
4. THE Sequencer SHALL support a per-step Step Probability in addition to a per-Pattern Step Probability; WHEN a step has an explicit per-step probability, THAT value SHALL take precedence over the per-Pattern value for that step, and WHEN a step has no explicit per-step probability it SHALL inherit the per-Pattern value.
5. WHEN a Deterministic Seed is set for the Pattern, THE sequence of trigger/suppress decisions SHALL be identical across repeated runs from the same starting position and across a save/load round-trip.
6. THE probabilistic decision SHALL execute on the Hot Path within the Milestone 2 bounded, allocation-free constraints.

### Requirement 3: Velocity humanization within bounds

**User Story:** As a performer, I want emitted velocities to vary slightly around their stored
values, so that generated parts feel less mechanical while staying under my control.

#### Acceptance Criteria

1. WHEN a Pattern has a non-zero Velocity Humanization range, THE Sequencer SHALL perturb each emitted velocity by a bounded amount within that symmetric range around the stored velocity.
2. THE Sequencer SHALL clamp every humanized velocity to the valid MIDI velocity range such that a note-on velocity is never zero (which would read as a note-off) and never exceeds 127.
3. WHEN the Velocity Humanization range is zero, THE Sequencer SHALL emit the stored VelocitySet velocity unchanged, preserving current behavior exactly.
4. WHEN a Deterministic Seed is set for the Pattern, THE sequence of humanized velocities SHALL be identical across repeated runs from the same starting position and across a save/load round-trip.
5. THE humanization computation SHALL execute on the Hot Path within the Milestone 2 bounded, allocation-free constraints.

### Requirement 4: Deterministic, recoverable randomness

**User Story:** As a performer, I want to repeat a useful generated result, so that a good
variation is not lost the moment it plays.

#### Acceptance Criteria

1. THE Sequencer SHALL derive all RANDOM PlayingOrder traversal, Step Probability decisions, and Velocity Humanization for a Pattern from that Pattern's generation random state.
2. WHEN a performer sets a Deterministic Seed for a Pattern, THE Sequencer SHALL reset that Pattern's generation random state to a defined function of the seed at the next defined musical boundary, so the same material replays identically.
3. WHEN a Pattern is saved and later loaded, THE saved Deterministic Seed SHALL reproduce the same generated material from the Pattern's defined starting position.
4. WHILE no Deterministic Seed is set, THE Sequencer SHALL use a defined default seeding behavior that is documented and still bounded and allocation-free on the Hot Path.
5. WHEN playback restarts after a normal Stop or Timing Fault, THE Sequencer SHALL re-establish each Pattern's generation random state consistently with its defined starting position so deterministic Patterns are reproducible.

### Requirement 5: Optional stream coupling for coherent phrases

**User Story:** As a performer, I want the pitch, velocity, and rhythm of a pattern to move
together as one phrase, so that a pattern has a recognizable character instead of three
independently drifting streams.

#### Acceptance Criteria

1. WHEN a performer enables Stream Coupling for a Pattern, THE Sequencer SHALL advance the coupled streams in the defined relationship to the PitchSet rather than on fully independent positions.
2. THE design SHALL define at least an "independent" coupling mode (current behavior) and a "locked" coupling mode in which velocity and/or gate selection follow the pitch stream's position.
3. WHEN Stream Coupling is "independent", THE Sequencer SHALL advance each stream exactly as it does today, preserving current behavior.
4. WHILE Stream Coupling is active and stream lengths differ, THE Sequencer SHALL apply a defined, documented rule for reconciling differing lengths without stranding notes or producing unbounded Hot Path work.
5. THE Stream Coupling mode SHALL be part of Working State and SHALL survive an explicit save and boot-time load round-trip unchanged.

### Requirement 6: Preservation of Milestone 2 performance-safe invariants

**User Story:** As a performer, I want the new generative controls to never compromise live
timing or note safety, so that expressiveness never costs reliability.

#### Acceptance Criteria

1. THE Generation Parameters evaluation for all Patterns SHALL complete within the per-tick Hot Path budget established in Milestone 2 and SHALL perform no heap allocation on the Hot Path.
2. WHEN any Generation Parameter changes during playback, THE change SHALL be applied through the existing Milestone 2 command/snapshot path at a defined musical boundary without tearing state or stranding notes.
3. THE new behavior SHALL preserve correct MIDI note lifecycle under every combination of Generation Parameters, such that no note is left sounding when its owning step is suppressed, skipped, or recoupled.
4. WHEN a Timing Fault occurs, THE Sequencer SHALL fail closed exactly as in Milestone 2 regardless of active Generation Parameters.
5. THE Generation Parameters SHALL serialize within the existing persistence record format, and a saved Pattern from before this milestone SHALL load with Generation Parameters set to their behavior-preserving defaults.

### Requirement 7: Legible performer feedback

**User Story:** As a performer, I want to see the active generative constraints on the LED
matrix, so that I can steer generation without text menus.

#### Acceptance Criteria

1. THE active Scale Constraint, Step Probability, Velocity Humanization, and Stream Coupling state for the selected Pattern SHALL be representable on the LED matrix without relying on text-heavy menus.
2. WHEN a Generation Parameter changes, THE matrix feedback SHALL reflect the new value within the existing UI responsiveness bound established in Milestone 2.
3. WHERE matrix resolution cannot show a full parameter value, THE design SHALL define a legible reduced representation (for example, a coarse level indicator) rather than omitting feedback.

### Requirement 8: Scale membership indication in the PitchSet view

**User Story:** As a performer editing a PitchSet, I want to see whether the currently selected
pitch lies inside the active scale, so that I can program pitches that will sound as written
rather than being silently quantized or skipped at emission.

#### Acceptance Criteria

1. WHILE the PitchSet view has a pitch position selected (`PITCH_SET_FIELD_PITCH_BASE` or above) and a Scale Constraint is active for the Pattern, THE PitchSet view SHALL indicate on the LED matrix whether the currently displayed pitch is in the active scale or out of scale.
2. THE in-scale versus out-of-scale indication SHALL be visually distinguishable from the existing dirty-draft indicator and from the pitch/position labels, and SHALL not obscure the rendered note name.
3. WHEN the performer changes the selected pitch position or adjusts the displayed pitch value, THE membership indication SHALL update to reflect the newly displayed pitch within the existing UI responsiveness bound established in Milestone 2.
4. THE membership indication SHALL be computed against the same Scale Constraint definition the Sequencer uses at emission (Requirement 1), so that the view and the audible result agree about which pitches are in scale.
5. WHEN no Scale Constraint is active for the Pattern, THE PitchSet view SHALL render exactly as it does today, with no membership indication and no change to existing pitch/position/order feedback.
6. THE membership indication SHALL be a read-only cue only; it SHALL NOT alter the stored PitchSet pitches, the draft edit, or the commit/undo behavior of the PitchSet view.

### Requirement 9: Per-step gate and probability editing in the GateSet view

**User Story:** As a performer defining a rhythm, I want to optionally seed the gate pattern with
the Euclidean generator and then walk through each step and set its gate (on/off) and its trigger
probability individually, all within the GateSet view, so that I can build and shape a rhythm
step by step without leaving the view where triggers are defined.

#### Acceptance Criteria

1. THE GateSet view SHALL provide a per-step editing mode, reachable through the established GateSet property model (alongside ALGORITHM, STEPS, PULSES, ROTATION, NOTE_LENGTH, LENGTH) and using the established move-property / adjust-value / ENTER-commit / ESCAPE-undo interaction without introducing a text-heavy menu, in which every step of the Pattern exposes both an editable gate state and an editable Step Probability.
2. THE GateSet view SHALL let the performer select a step and toggle its gate on or off manually, such that the stored gate pattern can be edited step by step independently of the Euclidean generator.
3. THE Euclidean generator (ALGORITHM / STEPS / PULSES / ROTATION) SHALL act as an optional seed for the per-step grid: applying the generator populates the gate states, and subsequent manual per-step gate and probability edits layer on top of the generated result. Re-applying the generator SHALL regenerate the gate states; the design SHALL define whether and how per-step probability values are preserved or reset when the generator is re-applied.
4. THE GateSet view SHALL let the performer set a per-step Step Probability for each step, and that value SHALL be the same per-step Step Probability consumed by the Sequencer in Requirement 2 (AC 4), so the edited value governs the audible trigger/suppress decision for that step.
5. WHEN a step has no explicit per-step probability set, THE Sequencer SHALL apply the Pattern's per-Pattern Step Probability to that step (consistent with Requirement 2, AC 4), and THE GateSet view SHALL represent that "inherit" state as distinct from an explicit 100 percent.
6. THE GateSet view SHALL indicate on the LED matrix which step is currently being edited, that step's gate state, and that step's probability value (or inherit state), using a legible representation per Requirement 7 (AC 3) where matrix resolution is limited.
7. WHEN the performer leaves all steps' probability at inherit and the per-Pattern Step Probability is 100 percent, THE Sequencer SHALL trigger every gated step, preserving current behavior exactly (consistent with Requirement 2, AC 3); a gate pattern built or edited only through the generator with no manual change SHALL remain byte-identical to today's generated gate pattern.
8. THE per-step gate and probability edits SHALL flow through the same Working State, draft/commit, and command/snapshot path as other GateSet edits, SHALL be bounded and allocation-free on the Hot Path, and SHALL survive an explicit save and boot-time load round-trip unchanged.

### Requirement 10: Simple UI controllability of every Generation Parameter

**User Story:** As a performer, I want every generative control added in this milestone to be
settable and clearable directly from the matrix UI in a simple way, so that I can actually use
each feature live without a host tool, a serial console, or a future interaction-design
milestone.

#### Acceptance Criteria

1. THE design SHALL provide a matrix-UI control path for every Generation Parameter introduced in this milestone (Scale Constraint, Quantization Mode, per-Pattern and per-step Step Probability, Velocity Humanization range, Deterministic Seed, and Stream Coupling mode), such that no Generation Parameter is reachable only through host-control, serial, or persistence tooling.
2. EACH such control SHALL reuse the established view interaction vocabulary (move-property / adjust-value / ENTER-commit / ESCAPE-undo, or the pattern-select model) rather than introducing a new text-heavy menu or a modal input method.
3. EACH such control SHALL let the performer return the parameter to its behavior-preserving default (no scale, 100 percent probability / inherit, zero humanization, independent coupling, no explicit seed) through the UI, so any feature can be simply turned off as well as on.
4. THE number of discrete UI actions required to set any single Generation Parameter from its default SHALL be bounded and small, consistent with the shallow-interaction principle, and THE design SHALL state that action count per control.
5. WHERE a Generation Parameter is edited while playback is live, THE UI control SHALL apply the change through the Milestone 2 command/snapshot path at a defined musical boundary (Requirement 6, AC 2), so live control never tears state or strands notes.
6. THE acceptance of this milestone SHALL require that each implemented Generation Parameter has a demonstrated matrix-UI set-and-clear path; a parameter implemented in the engine without such a UI path SHALL be considered incomplete for this milestone.

## Out of Scope

- Time-evolving mutation or variation operators (deferred to the next slice of this milestone).
- Scenes, arrangement, or project organization (Milestone 5).
- Any change to MIDI transport, multiple outputs, or MIDI CC generation (Milestone 6).
- Final hardware control selection (Milestone 7).
- Final interaction-design and control ergonomics (dedicated hardware controls, gesture vocabulary, optimized action counts). This milestone requires only a *minimum viable* Simple Control for each parameter (Requirement 10); refining those controls into their final form is a later milestone.
