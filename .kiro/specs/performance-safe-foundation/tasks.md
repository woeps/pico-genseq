# Implementation Plan: Performance-Safe Minimum Foundation

## Overview

This plan implements Milestone 2 in safety-first increments. It begins by making time
controllable and measuring the current behavior, then fixes note ownership before changing the
scheduler. It next introduces the global timeline, transport, reconciliation, MIDI output
bounds, cross-core handoff, and quantized operations. Persistence is addressed after the
real-time path is measurable: internal flash is benchmarked, an explicit backend decision is
made, and transactional storage is implemented. The milestone closes only after automated and
hardware evidence satisfies the approved timing and safety gates.

No new generative algorithms, final hardware controls, MIDI input, or MIDI CC behavior are part
of this plan.

## Tasks

- [ ] 1. Establish deterministic host time and baseline instrumentation
  - [x] 1.1 Define an SDK-free `IMonotonicClock` and firmware `PicoMonotonicClock`
    - Express time as monotonic 64-bit microseconds
    - Inject the clock into the scheduler/sequencer without changing MIDI transport ownership
    - _Requirements: 1.1, 3.1, 11.1_
  - [x] 1.2 Add a controllable `FakeMonotonicClock` to the host harness
    - Drive the production tick path at exact, early, and late timestamps
    - Cover wrap-safe arithmetic and representative BPM values
    - _Requirements: 3.1, 11.1, 11.2_
  - [x] 1.3 Add development timing metrics and compile-time trace points
    - Track maximum lateness, missed ticks, reconciliation, queue high-water marks, and fault cause
    - Provide trace points for tick, MIDI dispatch, operation apply, save, and fault transitions
    - Ensure release builds do not print from timing-critical code
    - _Requirements: 3.8, 4.4, 7.10, 11.5_
  - [x] 1.4 Capture and document the current hardware baseline
    - Measure normal tick timing, maximum-pattern output, edit traffic, and current save pause
    - Separate logical scheduling jitter from UART wire serialization
    - Store commands, analyzer configuration, captures, and observed maxima
    - _Requirements: 9.2, 9.3, 11.6, 11.7_

- [ ] 2. Implement canonical channels and per-pattern note ownership
  - [x] 2.1 Define canonical MIDI channel conversion and validation
    - Represent protocol channels as 0..15 internally and user channels as 1..16 only at boundaries
    - Remove the current channel 0/1 alias and cover all 16 channels
    - _Requirements: 5.1, 5.9_
  - [x] 2.2 Introduce stable `PatternId` runtime identity
    - Prevent removal/reordering from retargeting pending operations or ownership
    - Add stale-generation rejection if identifiers can be reused
    - _Requirements: 5.1, 6.9, 7.8_
  - [x] 2.3 Add a centralized fixed-capacity `NoteLifecycle`
    - Track each Pattern's exact sounding note plus aggregate channel/note ownership
    - Emit every approved retrigger and defer final Note Off until the last owner releases
    - Provide release-pattern, release-all, and panic operations
    - _Requirements: 5.1–5.5, 5.8_
  - [x] 2.4 Route normal gate edges and Stop through `NoteLifecycle`
    - Falling edges release the captured onset rather than the current pitch value
    - Stop leaves all ownership empty and resets Pattern runtime positions
    - Define deterministic repeated-Play behavior
    - _Requirements: 5.2, 5.8, 5.10_
  - [x] 2.5 Make live destructive operations note-safe
    - Mute/remove release ownership at their effective boundary
    - Gate/pitch/velocity replacement lets the current note finish and affects the next note
    - Handle empty sets and all-high gate sequences without stranded notes
    - _Requirements: 5.6, 5.7_
  - [x] 2.6 Add model-based note lifecycle tests
    - Cover overlaps, retriggers, edits, mute, remove, Stop, panic, repeated Play, and fault cleanup
    - Assert every generated trace ending in Stop/fault has zero ownership
    - _Requirements: 5.1–5.10, 11.2, 11.3_

- [ ] 3. Implement the global Transport Timeline and meter
  - [x] 3.1 Define supported Time Signature ranges
    - Choose bounded numerator and denominator sets exactly representable at the selected PPQN
    - Reject unsupported values rather than rounding boundaries
    - Record the decision in the design
    - _Requirements: 1.3, 1.7_
  - [x] 3.2 Implement `TransportTimeline`
    - Own BPM, PPQN, meter, logical tick index, beat/bar indices, and intended timestamps
    - Preserve phase across BPM changes using fixed-point or remainder accumulation
    - _Requirements: 1.1–1.5_
  - [x] 3.3 Align Pattern start/stop behavior with the timeline
    - Patterns keep independent cycle lengths while sharing transport boundaries
    - Normal restart and fault recovery begin from defined starting positions
    - _Requirements: 1.4, 1.6_
  - [x] 3.4 Add exhaustive meter and boundary tests
    - Cover each supported denominator/numerator, BPM 40/120/255, and long-run phase
    - _Requirements: 1.1–1.7, 11.2_

- [ ] 4. Complete MIDI transport and bounded output serialization
  - [x] 4.1 Implement the unified MIDI Clock Output setting
    - Enabled: exactly one Start, 24 Clock messages per quarter note, exactly one Stop
    - Disabled: no Start/Clock/Stop while notes continue normally
    - Preserve compatibility with persisted `midiClockEnabled`
    - _Requirements: 2.1–2.6_
  - [x] 4.2 Add a fixed-capacity MIDI event queue
    - Deterministic ordering for coincident events
    - Legal priority/interleaving for MIDI real-time messages
    - Reserved capacity or bounded direct handling for release/Stop/panic
    - _Requirements: 4.1, 4.2, 4.5, 4.7_
  - [x] 4.3 Add output backlog accounting and overload detection
    - Track current and maximum backlog
    - Enter Timing Fault after one beat of sustained wire-capacity overload
    - Never retain an unbounded delayed-event queue
    - _Requirements: 4.4–4.6_
  - [x] 4.4 Add host MIDI stream and ordering tests
    - Verify transport on/off, coincident note ordering, real-time interleaving, and overload
    - Keep logical scheduling assertions separate from modeled wire skew
    - _Requirements: 2.1–2.6, 4.1–4.7, 11.2_

- [ ] 5. Implement deterministic scheduler recovery and Timing Fault
  - [x] 5.1 Refactor sequencing behind `Scheduler::service(now)`
    - Give every tick an intended timestamp and documented per-service work budget
    - Prioritize immediate safety work before ordinary edits
    - _Requirements: 3.1–3.3_
  - [x] 5.2 Implement bounded under-one-beat reconciliation
    - Advance missed Pattern positions without obsolete Note On output
    - Emit required release events for notes whose release point was crossed
    - Resume at the authoritative phase without catch-up bursts
    - _Requirements: 3.4–3.6_
  - [x] 5.3 Implement fail-closed recovery at one beat
    - Stop new events, release ownership, emit optional Stop, preserve Working State
    - Latch cause and metrics; prohibit automatic restart
    - _Requirements: 3.7, 10.1–10.7_
  - [x] 5.4 Add fake-clock scheduler and fault tests
    - Cover just-under, exactly-at, and beyond-one-beat lateness
    - Verify no catch-up bursts and explicit Play restart from the beginning
    - _Requirements: 3.1–3.8, 10.1–10.7, 11.1–11.3_

- [ ] 6. Replace blocking cross-core payload transfer
  - [x] 6.1 Design a fixed-capacity shared-SRAM `CommandInbox`
    - Define slot ownership, synchronization, memory bound, and compact notification strategy
    - Keep core1 independent of blocking multiword FIFO payload reads
    - _Requirements: 7.1–7.4_
  - [x] 6.2 Add sequence IDs and accepted/applied acknowledgements
    - Bind updates to stable Pattern identities
    - Reject stale Pattern generations
    - _Requirements: 6.9, 7.8_
  - [x] 6.3 Add priority, coalescing, barriers, and saturation policy
    - Prioritize Stop/panic/fault
    - Coalesce compatible replaceable edits latest-wins
    - Never coalesce across transport, remove, or snapshot barriers
    - Never block indefinitely when full
    - _Requirements: 7.5–7.7, 7.11_
  - [x] 6.4 Remove steady-state allocation and logging from core1 command handling
    - Use fixed-capacity Pattern snapshots and bounded per-cycle draining
    - _Requirements: 7.4, 7.9, 7.10_
  - [ ] 6.5 Add inbox stress and model tests
    - Exercise edit storms, full inbox, stale sequences, barriers, and producer/consumer interleaving
    - Assert complete payload visibility and bounded service work
    - _Requirements: 7.1–7.11, 11.2_

- [ ] 7. Add immediate, beat, and bar operation application
  - [x] 7.1 Implement `TimingPolicy` and effective-tick calculation
    - Resolve Immediate, Next Beat, and Next Bar from the Transport Timeline
    - _Requirements: 6.1, 6.8_
  - [ ] 7.2 Encode approved per-operation defaults
    - Immediate: Play/Stop/panic/audition/Keep/Undo
    - Next Beat: mute/unmute
    - Next Bar: committed set replacement/remove and future scenes
    - _Requirements: 6.2–6.6_
  - [ ] 7.3 Implement the global timing override
    - Override musical operations while safety actions remain immediate
    - _Requirements: 6.7_
  - [ ] 7.4 Apply complete Pattern snapshots atomically at boundaries
    - Avoid partial gate/pitch/velocity state and preserve current sounding-note lifecycle
    - _Requirements: 5.7, 6.8, 7.1_
  - [ ] 7.5 Add boundary and coalescing integration tests
    - Queue operations before, on, and after boundaries across supported meters
    - Assert zero logical-tick application error
    - _Requirements: 6.1–6.9, 11.2_

- [ ] 8. Make save capture consistent and nonblocking
  - [ ] 8.1 Define Save Snapshot semantics in code
    - Capture accepted Working State atomically on core0
    - Include accepted quantized changes and exclude unkept drafts
    - Ensure later edits cannot mutate an in-progress snapshot
    - _Requirements: 8.1–8.4_
  - [ ] 8.2 Refactor persistence orchestration into a bounded service operation
    - Avoid monopolizing the UI loop
    - Preserve existing distinct matrix outcome feedback
    - Track UI/input service gaps
    - _Requirements: 8.5–8.9_
  - [ ] 8.3 Add snapshot and save-coordination host tests
    - Cover draft/working/pending distinctions, mutation after capture, timeout, and failure
    - Assert unsafe coordination performs zero destructive writes
    - _Requirements: 8.1–8.9, 11.2_

- [ ] 9. Storage benchmark and backend decision checkpoint
  - [ ] 9.1 Harden the existing internal-flash pause protocol before benchmarking
    - Use explicit synchronization and absolute-time timeout
    - Return positive acknowledgement/failure
    - Prohibit erase/program after timeout
    - _Requirements: 8.8, 9.8_
  - [ ] 9.2 Benchmark internal flash under worst-case live load
    - Measure scheduler jitter, MIDI continuity, UI/input gaps, pause, erase, program, and verify
    - Audit the complete RAM-resident path if sequencing continues while XIP is unavailable
    - _Requirements: 8.5–8.7, 9.2–9.4, 11.6, 11.7_
  - [ ] 9.3 Evaluate external nonvolatile storage
    - Include FRAM and document capacity, latency, endurance, bus/pin use, cost, and complexity
    - Prototype enough of the leading option to measure worst-case behavior
    - _Requirements: 9.1, 9.4, 9.5_
  - [ ] 9.4 Decision checkpoint: select the v1 backend
    - Compare measured results with the approved 1 ms and 100 ms limits
    - Record the chosen backend and rejected alternatives
    - Obtain user approval before committing final hardware-specific integration
    - _Requirements: 9.2–9.5, 9.9_

- [ ] 10. Implement transactional persistence on the selected backend
  - [ ] 10.1 Define backend-neutral record-store operations
    - Keep serialization/orchestration independent from internal flash or external hardware
    - _Requirements: 9.1_
  - [ ] 10.2 Implement A/B slots or equivalent transactional commit
    - Keep the previous record valid until the new record is written and verified
    - Use generation and commit state sufficient for deterministic boot selection
    - _Requirements: 9.6, 9.7_
  - [ ] 10.3 Integrate and validate the selected backend
    - Meet transparent MIDI and UI bounds during repeated saves
    - Preserve the previous record under timeout, injected failure, and interrupted commit
    - _Requirements: 8.5–8.9, 9.6–9.9_

- [ ] 11. Integrate persistent matrix fault feedback
  - [ ] 11.1 Add a distinct latched Timing Fault state to UI state
    - Preserve Working State and distinguish it from save/load feedback
    - _Requirements: 10.3, 10.4, 10.7_
  - [ ] 11.2 Render persistent LED-matrix fault indication
    - Do not auto-dismiss; keep input polling active
    - _Requirements: 10.4, 10.5_
  - [ ] 11.3 Make explicit Play acknowledge and restart
    - Clear the fault, reset runtime positions/pending invalid operations, start a new timeline
    - _Requirements: 10.5, 10.6_

- [ ] 12. Final automated and hardware validation
  - [ ] 12.1 Make required host tests reproducible
    - Register all required suites with CTest
    - Pin dependencies or provide always-available deterministic generated traces
    - Run sanitizer builds where supported
    - _Requirements: 11.1–11.4_
  - [ ] 12.2 Run the firmware and host build gates
    - Build firmware and run all required host suites with zero failures
    - Confirm zero steady-state real-time allocation and documented queue budgets
    - _Requirements: 3.3, 7.2, 7.4, 7.9, 11.2–11.4_
  - [ ] 12.3 Run RP2040 timing and MIDI validation
    - BPM 40/120/255, supported meters, minimum/maximum Pattern load, edit storms, and overload
    - Confirm logical jitter <=1 ms; report wire skew separately
    - Confirm normal UI gaps <=50 ms and save-time gaps <=100 ms
    - _Requirements: 3.2, 4.1–4.6, 8.5–8.7, 11.5–11.7_
  - [ ] 12.4 Run note-safety and endurance validation
    - Exercise mute/remove/edit/Stop/fault/save and overlapping notes
    - Run long-duration playback and repeated save/power-cycle tests
    - Confirm zero stuck notes, unexplained resets, invalid writes, or ownership leaks
    - _Requirements: 5.1–5.10, 9.6–9.9, 11.8_
  - [ ] 12.5 Retain evidence and reconcile documentation
    - Check in procedures, captures/results, counter dumps, storage decision, and known limits
    - Update README and technical docs to match measured behavior
    - Link evidence from ROADMAP.md and mark Milestone 2 complete only when every gate passes
    - _Requirements: 11.5–11.8_

## Checkpoints

- **After task 2:** note ownership and destructive operations are safe under host tests.
- **After task 5:** deterministic timeline, transport, reconciliation, and fail-closed behavior pass host tests.
- **After task 7:** editing cannot block core1 and approved quantized boundaries are functional.
- **After task 9:** storage backend decision is reviewed and approved before hardware-specific commitment.
- **After task 12:** all automated and hardware evidence gates pass; only then is Milestone 2 complete.

## Task Dependency Graph

```json
{
  "waves": [
    { "id": 0, "tasks": ["1.1", "1.2", "1.3"] },
    { "id": 1, "tasks": ["1.4", "2.1", "2.2", "3.1"] },
    { "id": 2, "tasks": ["2.3", "3.2"] },
    { "id": 3, "tasks": ["2.4", "2.5", "3.3", "4.1"] },
    { "id": 4, "tasks": ["2.6", "3.4", "4.2", "5.1"] },
    { "id": 5, "tasks": ["4.3", "5.2", "5.3", "6.1", "6.2"] },
    { "id": 6, "tasks": ["4.4", "5.4", "6.3", "6.4", "7.1"] },
    { "id": 7, "tasks": ["6.5", "7.2", "7.3", "8.1", "9.1"] },
    { "id": 8, "tasks": ["7.4", "8.2", "9.2", "9.3", "11.1"] },
    { "id": 9, "tasks": ["7.5", "8.3", "9.4", "11.2"] },
    { "id": 10, "tasks": ["10.1", "11.3"] },
    { "id": 11, "tasks": ["10.2"] },
    { "id": 12, "tasks": ["10.3", "12.1"] },
    { "id": 13, "tasks": ["12.2", "12.3", "12.4"] },
    { "id": 14, "tasks": ["12.5"] }
  ]
}
```
