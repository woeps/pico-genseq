# GenSeq v1 Product Charter

## Status

**Approved**

This charter defines the product direction for GenSeq v1. Detailed requirements and implementation designs remain owned by their corresponding roadmap milestones.

## 1. Product Vision

**GenSeq v1 is a standalone, live-performance generative MIDI sequencer for improvising electronic musicians, synth owners, and experimental composers.**

It enables performers to build, layer, mute, and transition between evolving musical sequences without relying on a computer during performance.

## 2. Core Promise

> **GenSeq helps me improvise complete sequences during a live performance, without a computer.**

GenSeq differs from a conventional step sequencer by combining repeatable musical foundations with deliberate, performer-controlled generation and mutation. It differs from an arpeggiator by supporting multiple independent patterns, richer rhythmic structures, prepared material, and live arrangement.

## 3. Target Performer

The primary user:

- Performs electronic music with one or more MIDI-controlled instruments.
- Understands common synthesizer, MIDI, and sequencing concepts.
- May prepare material before a performance but wants freedom to restructure and transform it live.
- Values experimental and generative composition.
- Wants useful complexity presented clearly rather than hidden by oversimplification.
- Does not want a computer to be part of the live-performance setup.

GenSeq may assume substantial musical and technical knowledge, but its state and actions should remain understandable. Advanced functionality should not require the performer to reason about the implementation or underlying algorithms while playing.

## 4. Core Performance Loop

The primary interaction loop is:

> **Build → Layer → Mute → Transition**

### Build

Create or load patterns and shape their rhythmic, pitch, velocity, duration, and generative behavior.

### Layer

Combine independent patterns into a more complete musical result. Patterns may:

- Share a MIDI channel and contribute to one instrument part.
- Use different MIDI channels to control separate sounds or a multitimbral synthesizer.

### Mute

Introduce and remove patterns as immediate performance gestures. Muting is a first-class operation, not a configuration detail.

### Transition

Move from the current musical state toward another state through:

1. Direct manual actions.
2. Changes scheduled for musical boundaries.
3. Coordinated scene recall.
4. Gradual transitions in a later iteration.

Prepared material is a starting point rather than a fixed backing track. The performer remains free to edit, mutate, rearrange, and return to known material throughout the set.

## 5. Musical Model

A GenSeq performance consists of multiple independent patterns.

Each pattern represents a musical voice with its own:

- Gate or rhythmic behavior.
- Pitch material.
- Velocity behavior.
- Note duration.
- Generative configuration.
- Active or muted state.
- MIDI channel.

Patterns can be layered freely. GenSeq does not impose whether those layers form one complex voice or several instrument parts.

The v1 interface focuses on note-oriented sequencing. MIDI CC generation is not required in the v1 interface, but the engine should provide a clean extension path so automation can be introduced later without redesigning the central sequencing model.

## 6. Generative Philosophy

GenSeq uses a hybrid model of intention and surprise:

- Musical material is stable and repeatable by default.
- Evolution or mutation occurs because the performer enables, requests, or controls it.
- Randomness operates within understandable musical constraints.
- A pattern should remain recognizable unless the performer deliberately pushes it further.
- Useful generated results can become intentional working material.
- Generative actions must be reversible.

The governing principle is:

> **Generation must remain steerable, legible, and recoverable during performance.**

GenSeq should create opportunities for discovery without taking authorship away from the performer.

## 7. Working-State and Recovery Model

GenSeq distinguishes between four layers of intent:

1. **Auditioned state:** a temporary edit or generated variation being heard.
2. **Working state:** the material currently accepted for the performance.
3. **Undo state:** enough recent history to reverse a meaningful operation.
4. **Saved baseline:** the last explicitly saved or loaded durable state.

The essential operations are:

- **Keep:** promote an auditioned or generated result into the current working state.
- **Undo:** reverse the most recent meaningful live operation.
- **Revert:** restore material from its saved or loaded baseline.
- **Save:** explicitly make the current working state durable.

Saving is explicit. GenSeq does not continuously overwrite prepared material. Unsaved changes may be discarded after reload or power loss.

Snapshots and broader history are deferred to the scenes/projects milestone. They are not prerequisites for the initial generative workflow.

## 8. Timing and Transition Model

GenSeq supports both immediate auditioning and musically quantized changes.

- Immediate response is useful while preparing and exploring material.
- Quantized application is generally safer during live performance.
- Different actions may have sensible default timing behavior.
- The performer should be able to select or override the timing policy where appropriate.
- Structural operations must not create malformed rhythms, hanging notes, or unintended timing discontinuities.

The initial timing vocabulary should remain small and legible—for example, immediate, beat-boundary, and bar-boundary—rather than exposing arbitrary scheduling complexity.

The exact per-operation defaults belong to later workflow and foundation design, not this charter.

## 9. Interaction Philosophy

GenSeq balances performance immediacy with detailed editing:

- Frequent performance actions must be directly accessible.
- Detailed editing should remain available through shallow, understandable modes.
- Important live state must be visible on the LED matrix.
- The interface should favor recognition over memorization.
- Deep menus and text-heavy workflows should be avoided.
- Complexity should be progressive rather than removed.

The product is allowed to be an expert instrument, but it should not be an unnecessarily cryptic one.

## 10. Hardware and Prototyping Strategy

### Prototype

- Raspberry Pi Pico/RP2040 platform.
- 16×16 LED matrix as the primary and only planned display.
- USB keyboard as the temporary prototyping control surface.
- One serial MIDI output.
- No computer required during normal performance.

The keyboard is used to discover:

- Which actions require immediate access.
- Which modifier combinations remain understandable.
- Which operations are frequently used together.
- Which state must be visible at a glance.
- Which controls should become dedicated buttons.

### Final Product

- Dedicated physical controls replace the keyboard.
- The LED matrix remains the primary visual interface.
- The device is contained in a single housing.
- No LCD is planned.
- The final control layout is chosen only after the keyboard workflow has been validated.

The prototype should avoid interaction patterns that cannot reasonably be translated to a dedicated hardware panel.

## 11. MIDI Role

GenSeq is a focused sequence generator, not a central MIDI hub.

The first iteration provides:

- One serial DIN MIDI output.
- Independent per-pattern MIDI channels.
- Correct note and transport behavior.
- Reliable synchronization behavior where required.
- Integration with a synthesizer, drum machine, multitimbral instrument, or downstream MIDI chain.

Out of scope:

- Multiple MIDI outputs.
- General-purpose routing.
- MIDI merging.
- Complex filtering or transformation.
- Broad USB MIDI connectivity.
- DAW-controller functionality.

Serial MIDI input may be considered later for external clock, transport, or focused performance control. It should remain an extension rather than redefine the product.

## 12. Explicit Product Boundaries

GenSeq v1 is not:

- A traditional linear DAW.
- A general-purpose MIDI workstation.
- A MIDI routing hub.
- A full arranger keyboard.
- A text- or menu-driven device.
- A replacement for every sequencing style.
- A system that evolves unpredictably without performer intent.
- A device that requires a computer during performance.

## 13. Product Principles

1. **Performance first:** frequent actions must be immediate, predictable, and safe.
2. **Stable by default:** generative behavior should not destroy recognizable material without intent.
3. **Controlled surprise:** randomness is constrained and performer-directed.
4. **Reversible exploration:** useful accidents can be kept; unwanted ones can be undone or reverted.
5. **Matrix-native feedback:** the workflow must make sense on the LED matrix without an LCD.
6. **Independent voices:** patterns can form one layered part or multiple MIDI-channel parts.
7. **Musical timing:** structural changes respect selectable musical boundaries.
8. **No stuck notes:** every live operation preserves a correct note lifecycle.
9. **Explicit durability:** working material is saved deliberately.
10. **Progressive complexity:** advanced functionality remains available but understandable.
11. **Prototype before committing hardware:** the final panel follows the validated workflow.
12. **Focused connectivity:** sequencing and performance take priority over MIDI utility features.

## 14. Current-State Gap Map

This map assigns known differences between the approved product direction and the current implementation. It is intentionally not a detailed feature specification.

| Charter need | Current state | Roadmap owner |
|---|---|---|
| Safe live transitions | Partially supported; transition behavior and timing guarantees are not yet defined | Milestone 2 |
| Stable generative foundation | Basic Euclidean gate and pitch/velocity order behavior exist | Milestones 2–3 |
| First-class pattern mute workflow | Sequencer/state support exists, but the operation is not exposed as a complete performance workflow | Milestone 4 |
| Quantized operations | No established immediate/beat/bar application policy | Milestones 2 and 4 |
| Keep, Undo, and Revert model | Draft editing supports limited commit/undo behavior; the approved cross-pattern model is not implemented | Milestones 3–4 |
| Prepared projects | One saved configuration is supported; musician-facing project organization is absent | Milestone 5 |
| Per-pattern MIDI channels | Reserved by persistence and represented in the sequencer, but runtime/UI configuration is incomplete | Milestone 6 |
| Dedicated physical controls | USB keyboard prototype only | Milestone 7 |
| Matrix-only product UI | Matrix is active, but obsolete LCD/encoder code and documentation assumptions remain | Milestones 4 and 7 |
| Note-focused engine with future CC extension | Note output exists; a general event extension path has not been designed | Milestones 2, 3, and 6 |

## 15. Milestone 1 Completion Criteria

Milestone 1 is complete because:

- [x] The product vision and one-sentence promise are approved.
- [x] The target performer and assumed experience level are documented.
- [x] The Build → Layer → Mute → Transition loop is accepted as the primary workflow.
- [x] Pattern, working-state, saved-baseline, Keep, Undo, Revert, and Save terminology is agreed.
- [x] The stable-by-default generative philosophy is approved.
- [x] Immediate versus quantized change behavior is defined at the product-policy level.
- [x] The LED-matrix-only display strategy is explicit.
- [x] The keyboard's role as a temporary workflow prototype is explicit.
- [x] The final dedicated-control direction is documented without prematurely selecting a button layout.
- [x] The single serial MIDI output scope and MIDI-hub exclusions are explicit.
- [x] Note-oriented v1 scope and future MIDI CC extensibility are documented.
- [x] Product non-goals are explicit enough to reject out-of-scope feature requests.
- [x] Existing implementation gaps are mapped to the appropriate later roadmap milestones.
- [x] No unresolved product decision blocks planning Milestone 2.

Milestone 1 does **not** require:

- Implementing new firmware behavior.
- Finalizing the physical button layout.
- Selecting exact generative algorithms.
- Designing scenes or arrangements in detail.
- Adding MIDI input or MIDI CC output.
- Producing detailed requirements for later milestones.

## 16. Next Decision Gate

Milestone 2 should define the performance-safety invariants and measurable acceptance criteria needed before expanding the generative engine. It should be planned as a separate requirements and design effort rather than extending this charter into an implementation specification.
