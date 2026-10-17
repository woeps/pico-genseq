# GenSeq Roadmap

## North Star

**GenSeq v1 is a standalone, live-performance generative MIDI sequencer that uses a 16×16 LED matrix for feedback and sends MIDI through one serial output.**

During prototyping, a USB keyboard lets us discover and refine the interaction model. Productization later translates the validated controls to dedicated hardware buttons in a single enclosure. The keyboard is therefore a temporary physical interface, not part of the final product identity.

## Roadmap

### 1. Product Identity: Define GenSeq v1

**Status: Complete — product charter approved.**

The approved [GenSeq v1 Product Charter](docs/product/genseq-v1-product-charter.md) defines:

- The target performer and core promise.
- The Build → Layer → Mute → Transition performance loop.
- Independent patterns as freely layerable MIDI voices.
- Stable-by-default, performer-controlled generation.
- Keep, Undo, Revert, and explicit Save semantics.
- Immediate auditioning with selectable, musically quantized live changes.
- A balanced performance and shallow-editing interaction model.
- The LED-matrix-only display strategy and keyboard prototyping role.
- One serial MIDI output, note-focused v1 scope, and explicit non-goals.

The charter also maps known implementation gaps to later milestones. Completing this product-definition milestone does not imply that those firmware capabilities are already implemented.

**Milestone outcome:** An approved product definition and decision framework guiding all later milestones.

### 2. Performance-Safe Minimum Foundation

**Status: Planned — approved specification ready for implementation.**

The approved Milestone 2 specification is split into:

- [Requirements](.kiro/specs/performance-safe-foundation/requirements.md)
- [Design](.kiro/specs/performance-safe-foundation/design.md)
- [Implementation Tasks](.kiro/specs/performance-safe-foundation/tasks.md)
- [Hardware Timing Baseline (task 1.4 evidence)](docs/development/baseline/README.md) — BS05U captures of the current implementation; logical jitter ≈ 0, blocking save stalls MIDI ~33 ms (the defect this milestone fixes).

This milestone establishes the invariants required before expanding the generative engine:

- One authoritative transport timeline with configurable meter.
- Correct optional MIDI Start, 24-PPQN Clock, and Stop output.
- Deterministic late-tick reconciliation and visible fail-closed recovery.
- Per-pattern note ownership with safe overlapping-note behavior.
- Immediate, beat, and bar operation boundaries.
- Bounded cross-core command and MIDI output queues.
- MIDI-transparent, transactional saving with a measured storage-backend decision.
- Reproducible host tests and retained RP2040 timing evidence.

The implementation proceeds through independently verifiable safety slices and includes a user-reviewed storage decision checkpoint before final backend integration.

**Milestone outcome:** A sequencer core that can be trusted during uninterrupted live performance.

### 3. Generative Engine Expansion

Build the musical vocabulary that makes GenSeq distinctive.

The current independent gate, pitch, and velocity streams are a strong basis. This milestone evolves them into a coherent generative system rather than a collection of unrelated algorithms.

The system should address:

- How material is generated.
- How variation develops over time.
- How the user constrains randomness.
- How patterns remain recognizable while evolving.
- How changes can be immediate, gradual, temporary, or repeatable.
- Which parameters are useful for live manipulation.

The primary design principle is:

> Generation must remain steerable, legible, and recoverable during performance.

**Milestone outcome:** A compelling generative engine with a clear musical character.

### 4. Musician Workflow

Shape the capabilities into an intentional live-performance interaction model.

The workflow will be designed and tested using the keyboard first, allowing us to learn which operations deserve dedicated hardware controls later. The LED matrix must communicate state without relying on text-heavy menus.

This milestone determines:

- What the performer sees on the main performance screen.
- How the performer moves between overview and detail.
- How patterns are selected, muted, edited, and manipulated.
- Which operations are global and which target a pattern.
- How temporary performance changes differ from committed edits.
- How mistakes are reversed quickly.
- Which actions need immediate one-press access.

This is where the current test screen and implementation-oriented navigation become a genuine instrument UI.

**Milestone outcome:** A validated interaction flow that can later be mapped onto physical buttons.

### 5. Scenes, Arrangement, and Projects

Add structure above individual patterns without turning GenSeq into a workstation.

The roadmap distinguishes three concepts:

- **Scene:** A recallable live state or coordinated set of pattern states.
- **Arrangement:** An optional progression or performance order of scenes.
- **Project:** The durable container holding the instrument's musical material and configuration.

Improvisation remains the primary mode. Arrangement supports performance rather than forcing everything onto a fixed linear timeline.

This milestone also gives persistence a musician-facing purpose beyond a single anonymous snapshot.

**Milestone outcome:** Performers can organize, recall, and develop complete sets while retaining live control.

### 6. MIDI Connectivity

Complete the focused connectivity needed for the instrument's role.

The first iteration includes:

- One serial DIN MIDI output.
- Correct and configurable behavior appropriate to that output.
- Reliable transport and synchronization where needed.
- Enough channel configuration to address the intended external instrument setup.

The following are explicitly out of scope:

- MIDI routing-hub behavior.
- Multiple outputs.
- Complex merging, filtering, or transformation.
- Broad USB MIDI topology.

A later iteration may add serial MIDI input for external clock, transport, or performance control. MIDI input remains an extension point and should not shape v1 prematurely.

**Milestone outcome:** Focused, reliable integration with a synth, drum machine, or downstream MIDI chain.

### 7. Productization

Translate the proven prototype into a self-contained instrument.

Only after the keyboard workflow has stabilized will the exact physical control layout be selected. Productization includes:

- Dedicated physical controls replacing the keyboard.
- The LED matrix retained as the sole primary display; no LCD is planned.
- A single-housing mechanical design.
- Power and MIDI electrical design.
- Final PCB and wiring.
- Boot, recovery, and firmware-update behavior.
- Removal of obsolete LCD/encoder assumptions and dead code.
- Build documentation and hardware validation.
- Release-level reliability and reproducibility.

**Milestone outcome:** A standalone GenSeq v1 that can be built, transported, and performed with without development peripherals.

## Cross-Cutting Principles

1. **Performance first.** Common musical actions must be immediate and safe.
2. **Generative, not arbitrary.** Randomness needs constraints and musical continuity.
3. **Matrix-native feedback.** Do not design workflows that depend on an LCD.
4. **Prototype controls before committing hardware.** The keyboard is a research tool for discovering the eventual panel.
5. **Shallow interaction during performance.** Important actions should not require deep navigation.
6. **No stuck notes.** Every operation must preserve a correct MIDI note lifecycle.
7. **Deterministic when requested.** A performer should be able to repeat or recall a useful result.
8. **Focused connectivity.** GenSeq generates and performs; it does not become a general MIDI utility.
9. **Progressive complexity.** Basic use remains accessible while deeper generation is available deliberately.
10. **Save musical intent.** Projects should eventually preserve the state required to reproduce the intended result, not just isolated parameter values.

## Milestone Dependencies

- Product identity constrains all later decisions.
- The performance-safe foundation must precede a more powerful generative engine.
- Generative capabilities should be explored before finalizing the workflow, because the workflow must expose the controls performers actually need.
- The workflow must stabilize before scenes/projects and before committing to physical controls.
- Basic MIDI correctness is part of the safe foundation; broader configuration and optional input belong to the connectivity milestone.
- Productization comes last so that the final panel embodies a tested workflow rather than an assumed one.
