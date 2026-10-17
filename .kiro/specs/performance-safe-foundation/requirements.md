# Requirements Document: Performance-Safe Minimum Foundation

## Introduction

This milestone makes GenSeq dependable during uninterrupted live performance before the
project expands its generative engine. The current firmware has a functional dual-core
sequencer, multi-pattern editing, serial MIDI output, and flash persistence, but it does not
yet provide bounded real-time behavior. The sequencer processes at most one overdue tick per
loop and therefore catches up in bursts, active notes are tracked globally rather than by
pattern, structural changes can strand notes, large commands block on the multicore FIFO,
MIDI Clock and Stop are incomplete, and internal-flash saving parks the sequencing core.

The milestone establishes one authoritative musical timeline, safe note ownership,
configurable MIDI Clock output, deterministic late-tick recovery, musically quantized
operations, bounded cross-core communication, transparent saving, visible fail-closed
behavior, and measurable host and hardware acceptance gates.

It does not add new generative algorithms, design the final hardware controls, add MIDI input,
add multiple MIDI outputs, or expose MIDI CC generation in the v1 interface.

## Glossary

- **Transport Timeline**: The global monotonic musical timeline shared by all patterns.
- **Logical Tick**: One scheduler step at the internal PPQN resolution, with an intended
  timestamp independent of when firmware happens to service it.
- **PPQN**: Pulses per quarter note. GenSeq currently uses 24 logical ticks per quarter note.
- **Time Signature**: Configurable numerator and denominator used to derive global beat and bar
  boundaries.
- **Pattern Identity**: A stable identifier for a pattern that is not invalidated when another
  pattern is removed or reordered.
- **Pattern Owner**: The pattern that initiated a sounding note and is responsible for
  releasing its logical ownership.
- **Aggregate Ownership**: The number of pattern owners currently holding the same MIDI
  channel/note pair.
- **Timing Policy**: The effective application boundary for a musical operation: Immediate,
  Next Beat, or Next Bar.
- **Reconciliation**: Advancing internal musical state after a bounded delay without replaying
  obsolete MIDI events in a burst.
- **Timing Fault**: A latched fail-closed state entered when scheduling lateness or sustained
  MIDI output overload reaches one beat.
- **Logical Scheduling Jitter**: Difference between an event's intended scheduler timestamp
  and the time it is dispatched to the MIDI output layer, excluding unavoidable DIN MIDI wire
  serialization.
- **Wire Skew**: The unavoidable delay between coincident MIDI messages caused by serializing
  them at 31,250 baud.
- **Working State**: Accepted musical configuration eligible for explicit save. It excludes
  unkept audition drafts.
- **Save Snapshot**: An immutable, internally consistent copy of Working State captured for one
  save operation.
- **Transparent Save**: A save that preserves MIDI timing and limits UI/input interruption to
  the approved bound.
- **Fail Closed**: Stop event generation, release notes, emit MIDI Stop when enabled, latch a
  visible fault, and require explicit restart.

## Requirements

### Requirement 1: Authoritative transport timeline and configurable meter

**User Story:** As a performer, I want every pattern and transition to use the same musical
timeline, so that layered material and quantized changes stay synchronized.

#### Acceptance Criteria

1. WHEN playback starts, THE Sequencer SHALL establish one monotonic Transport Timeline shared by every Pattern.
2. WHILE playback is active, THE Sequencer SHALL derive every Logical Tick, beat boundary, and bar boundary from the Transport Timeline rather than from an individual Pattern cycle.
3. WHEN the performer configures a supported Time Signature, THE Sequencer SHALL derive beats from its denominator and bars from its numerator.
4. WHILE Patterns have different sequence lengths, THE Sequencer SHALL allow their cycles to evolve independently without changing the global beat or bar boundaries.
5. WHEN BPM changes during playback, THE Sequencer SHALL preserve the current Transport Timeline phase and SHALL apply the new tick duration from a defined musical boundary without restarting Pattern positions.
6. WHEN playback starts after a normal Stop or Timing Fault, THE Sequencer SHALL begin every Pattern from its defined starting position.
7. THE implementation SHALL define supported Time Signature numerator and denominator ranges such that every supported beat boundary is exactly representable at the chosen PPQN.

### Requirement 2: Configurable MIDI Clock transport output

**User Story:** As a performer, I want GenSeq optionally to act as MIDI Clock master, so that I
can synchronize downstream equipment without forcing transport messages on every setup.

#### Acceptance Criteria

1. WHEN MIDI Clock Output is enabled and playback starts, THE MIDI output SHALL emit exactly one MIDI Start message before the first playback Clock or note event.
2. WHILE MIDI Clock Output is enabled and playback is active, THE MIDI output SHALL emit 24 MIDI Clock messages per quarter note from the same Transport Timeline used by Pattern sequencing.
3. WHEN MIDI Clock Output is enabled and playback stops normally or fails closed, THE MIDI output SHALL emit exactly one MIDI Stop message for that transition to stopped state.
4. WHILE MIDI Clock Output is disabled, THE MIDI output SHALL emit no MIDI Start, Clock, or Stop messages and SHALL continue to emit note messages normally.
5. WHEN MIDI Clock Output is changed, THE setting SHALL be represented in Working State and SHALL remain compatible with explicit persistence.
6. THE v1 setting SHALL control Start, Clock, and Stop together; independent transport-message configuration SHALL be deferred.

### Requirement 3: Deterministic scheduling and late-tick recovery

**User Story:** As a performer, I want stable timing without catch-up bursts, so that temporary
firmware delays do not produce musically destructive output.

#### Acceptance Criteria

1. WHEN a Logical Tick is scheduled under supported load, THE Scheduler SHALL assign it an intended monotonic timestamp.
2. WHILE no declared overload or fault condition exists, THE Scheduler SHALL dispatch each due logical event within 1 millisecond of its intended timestamp.
3. THE Scheduler SHALL perform a documented, bounded amount of work in each service cycle.
4. IF scheduling lateness is greater than zero but less than one beat, THEN THE Scheduler SHALL reconcile internal Pattern state to the current Transport Timeline position without emitting obsolete Note On events.
5. WHEN reconciliation crosses a release point for a note that was sounding before the delay, THE Scheduler SHALL emit the safety-critical Note Off required to release that note.
6. AFTER bounded reconciliation, THE Scheduler SHALL resume from the authoritative phase and SHALL NOT replay missed events as a rapid catch-up burst.
7. IF scheduling lateness reaches or exceeds one beat, THEN THE Scheduler SHALL enter the Timing Fault state.
8. THE Scheduler SHALL expose maximum observed lateness, missed Logical Tick count, reconciliation count, and Timing Fault cause for validation builds.

### Requirement 4: Deterministic MIDI serialization and overload protection

**User Story:** As a performer, I want dense layered output to degrade safely within the limits
of one serial MIDI port, so that bandwidth pressure never creates an unbounded delayed burst.

#### Acceptance Criteria

1. WHEN multiple MIDI messages share an intended timestamp, THE MIDI output layer SHALL serialize them in a documented deterministic order.
2. WHILE note messages are being serialized, THE MIDI output layer SHALL allow MIDI real-time Clock messages to receive priority and to be legally interleaved.
3. THE 1 millisecond scheduling target SHALL apply to logical dispatch and SHALL exclude unavoidable Wire Skew at 31,250 baud.
4. THE MIDI output layer SHALL expose current backlog and maximum observed backlog for validation builds.
5. THE MIDI output layer SHALL use bounded storage and SHALL NOT permit an unbounded delayed-event queue.
6. IF sustained output demand exceeds serial MIDI capacity for one beat, THEN THE Sequencer SHALL enter the Timing Fault state.
7. WHEN a safety-critical Note Off, Stop, or panic operation is required, THE MIDI output layer SHALL preserve capacity or a direct bounded path for that operation.

### Requirement 5: Per-pattern note ownership and safe release

**User Story:** As a performer, I want edits and overlapping patterns never to leave hanging
notes or silence one another incorrectly.

#### Acceptance Criteria

1. WHEN a Pattern emits Note On, THE Sequencer SHALL record the exact canonical MIDI channel and note as ownership belonging to that Pattern Identity.
2. WHEN the Pattern later releases the note, THE Sequencer SHALL use the recorded note identity rather than the Pattern's current edited pitch or channel.
3. WHEN multiple Patterns emit Note On for the same channel/note pair, THE MIDI output SHALL preserve each retrigger and THE Sequencer SHALL track every logical owner.
4. WHEN one of multiple owners releases a shared channel/note pair, THE Sequencer SHALL remove only that Pattern's ownership and SHALL NOT emit the final Note Off while another logical owner remains.
5. WHEN the final owner releases a channel/note pair, THE MIDI output SHALL emit the corresponding Note Off and SHALL clear aggregate ownership.
6. WHEN a Pattern is muted or removed at its effective boundary, THE Sequencer SHALL release every note owned by that Pattern before deactivating or destroying its runtime state.
7. WHEN gate, pitch, or velocity material is replaced, THE currently sounding note SHALL complete using its original recorded lifecycle and replacement material SHALL control the next note.
8. WHEN Stop, panic, or fail-closed recovery occurs, THE Sequencer SHALL release all owned notes and SHALL leave both per-pattern and aggregate ownership empty.
9. THE Sequencer SHALL use one canonical internal MIDI channel representation covering all 16 protocol channels without aliases.
10. WHEN Play is received while playback is already active, THE Sequencer SHALL behave deterministically and SHALL NOT abandon existing ownership or emit an unmatched transport transition.

### Requirement 6: Musically quantized operation application

**User Story:** As a performer, I want live changes to take effect at predictable musical
boundaries, so that transitions remain intentional while safety actions remain immediate.

#### Acceptance Criteria

1. THE system SHALL represent Immediate, Next Beat, and Next Bar Timing Policies against the global Transport Timeline.
2. WHEN Play, Stop, panic, continuous audition, Keep, or Undo is requested, THE operation SHALL use the Immediate policy by default.
3. WHEN Pattern mute or unmute is requested, THE operation SHALL use the Next Beat policy by default.
4. WHEN committed gate, pitch, or velocity material is replaced, THE operation SHALL use the Next Bar policy by default.
5. WHEN Pattern removal is requested, THE operation SHALL use the Next Bar policy by default.
6. WHEN a future scene transition is introduced, THE default application boundary SHALL be Next Bar unless its own approved requirements supersede this criterion.
7. WHEN the global Timing Policy override is changed, THE system SHALL apply Immediate, Next Beat, or Next Bar to musical operations while leaving Stop, panic, and fault handling immediate.
8. WHEN a queued operation reaches its selected boundary, THE Sequencer SHALL apply the complete operation atomically with zero Logical Tick boundary error.
9. WHILE an operation is pending, THE system SHALL bind it to a stable Pattern Identity rather than a mutable collection index.

### Requirement 7: Bounded cross-core command handoff

**User Story:** As a performer, I want editing and navigation never to block sequencing, so that
heavy control activity cannot disturb musical timing.

#### Acceptance Criteria

1. WHEN core0 submits a Pattern or transport update, THE command handoff SHALL publish a complete atomic message or snapshot to core1.
2. THE command handoff SHALL use fixed-capacity storage with a documented maximum memory footprint.
3. WHILE a command payload is incomplete or unavailable, core1 SHALL NOT block waiting for additional payload words.
4. THE core1 service loop SHALL process no more than a documented command budget before servicing due scheduler work.
5. Stop, panic, and fault operations SHALL have priority over ordinary musical edits.
6. WHEN compatible updates target the same replaceable Pattern property before application, THE handoff MAY coalesce them using a latest-wins policy.
7. THE handoff SHALL NOT coalesce across transport, Pattern removal, save snapshot, or other documented ordering barriers.
8. EVERY accepted operation SHALL receive a sequence identifier, and the system SHALL make its accepted and applied status observable to coordination code.
9. THE timing-critical receive, scheduling, and application paths SHALL perform no steady-state heap allocation.
10. Normal release builds SHALL perform no diagnostic printing from timing-critical paths.
11. IF the handoff reaches capacity, THEN THE system SHALL apply a documented bounded backpressure, coalescing, rejection, or fail-closed policy rather than blocking indefinitely.

### Requirement 8: Consistent and transparent save

**User Story:** As a performer, I want to save during playback without audible disruption, so
that preserving my work does not interrupt the set.

#### Acceptance Criteria

1. WHEN Save is requested, THE persistence coordinator SHALL capture one immutable and internally consistent Save Snapshot of accepted Working State.
2. THE Save Snapshot SHALL include accepted working changes even when their audible application is pending a quantized boundary.
3. THE Save Snapshot SHALL exclude unkept audition drafts.
4. WHEN Working State changes after snapshot capture, THE in-progress Save Snapshot SHALL remain unchanged.
5. WHILE a save is in progress, THE Scheduler and MIDI output SHALL continue meeting their normal logical timing budget and SHALL emit no missed, duplicated, delayed-burst, or replayed events attributable to saving.
6. WHILE a save is in progress, THE UI and input loop SHALL have no service gap greater than 100 milliseconds.
7. WHILE ordinary playback is active without saving, THE UI and input loop SHALL have no service gap greater than 50 milliseconds.
8. IF save coordination cannot establish a safe write condition within its documented deadline, THEN the save SHALL fail without performing a destructive storage write.
9. WHEN save completion or failure is reported, THE existing distinct LED-matrix feedback behavior SHALL remain available.

### Requirement 9: Backend-neutral and transactional persistence

**User Story:** As a performer, I want a failed or interrupted save to preserve my previous
configuration, so that live saving never destroys the last known-good project state.

#### Acceptance Criteria

1. THE persistence orchestration SHALL depend on a backend-neutral storage interface rather than directly on internal RP2040 flash.
2. BEFORE selecting the v1 storage backend, THE project SHALL benchmark internal RP2040 flash under worst-case supported playback and UI activity.
3. THE benchmark SHALL compare measured internal-flash behavior against the 1 millisecond MIDI scheduling and 100 millisecond UI/input requirements.
4. IF an internal-flash design cannot meet every transparent-save requirement with an auditable RAM-resident execution path, THEN the v1 design SHALL select an external nonvolatile backend.
5. THE external-storage evaluation SHALL include FRAM as a candidate and SHALL record capacity, latency, endurance, bus, component, and firmware trade-offs.
6. WHEN a new Save Record is written, THE storage protocol SHALL preserve the previous valid record until the new record has been completely written and verified.
7. IF power loss, timeout, write failure, or verification failure occurs, THEN the next boot SHALL load the most recent previously committed valid record.
8. WHEN a low-level pause or exclusive-access protocol is used, THE protocol SHALL report positive acknowledgement or explicit timeout, and a timeout SHALL prohibit the write.
9. THE selected backend and rejected alternatives SHALL be recorded with measured evidence before Milestone 2 is completed.

### Requirement 10: Visible fail-closed recovery

**User Story:** As a performer, I want timing failures to stop safely and visibly, so that I can
recover deliberately instead of continuing with corrupted musical state.

#### Acceptance Criteria

1. WHEN a Timing Fault is entered, THE Sequencer SHALL stop generating new Pattern events immediately.
2. WHEN a Timing Fault is entered, THE Sequencer SHALL release all owned notes and emit MIDI Stop if MIDI Clock Output is enabled.
3. WHEN a Timing Fault is entered, THE system SHALL preserve Working State and SHALL discard or safely resolve invalid pending runtime operations.
4. WHILE a Timing Fault remains latched, THE LED matrix SHALL display a persistent indication distinct from normal, save-success, save-failure, and load-failure states.
5. WHILE a Timing Fault remains latched, THE Sequencer SHALL not restart automatically.
6. WHEN the performer explicitly invokes Play while a Timing Fault is latched, THE system SHALL acknowledge the fault, reset Pattern runtime positions, and restart from the beginning.
7. THE system SHALL retain a machine-readable fault cause and relevant timing or backlog measurement for development diagnostics.

### Requirement 11: Measurable verification gate

**User Story:** As a developer and performer, I want performance claims backed by repeatable
evidence, so that Milestone 2 represents dependable behavior rather than subjective testing.

#### Acceptance Criteria

1. THE host test environment SHALL provide a controllable monotonic clock that drives the production scheduler tick path.
2. THE automated suite SHALL cover transport bytes, supported meter calculations, reconciliation, fail-closed behavior, note ownership, overlap, mutation, quantized operations, handoff saturation, coalescing, snapshot consistency, and storage failure.
3. FOR generated operation traces ending in Stop or Timing Fault, THE tests SHALL verify that no per-pattern or aggregate note ownership remains.
4. THE required host tests SHALL be registered with CTest and SHALL not silently disappear when an optional dependency is unavailable.
5. THE firmware SHALL expose an instrumentation mode sufficient to measure scheduler dispatch, MIDI output, command application, save activity, and faults on RP2040 hardware.
6. THE hardware validation SHALL measure representative minimum, typical, and maximum BPM, Pattern load, edit pressure, and save activity.
7. THE hardware validation SHALL separately report Logical Scheduling Jitter and unavoidable MIDI Wire Skew.
8. Milestone 2 SHALL NOT be marked complete until automated tests, firmware build, hardware timing, stuck-note stress, save-cycle, and storage-backend decision gates all pass with retained evidence.
