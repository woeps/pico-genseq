# Requirements Document

## Introduction

Today a pico-genseq pattern carries its own MIDI channel (`persistence::PatternConfig::midiChannel`,
`common::Pattern::midiChannel`, 1..16). The UI has no control for it, so every pattern plays on
channel 1 unless the save record says otherwise. The product charter (Layer step) says patterns
may share a MIDI channel to form one part or use different channels to drive separate sounds,
but the firmware has no object that represents "one part on one channel".

This feature adds an **Instrument**, a layer above patterns. An Instrument owns one MIDI channel
and holds a selection of patterns. A pattern no longer stores a channel of its own. A pattern
may be selected by several Instruments at once, and then it plays on the channel of each of
them. The pattern generates its notes once per step, and every Instrument that selects it
sends that same note on its own channel.

Each MIDI channel is used by at most one Instrument. To have two parts on one channel, the
performer puts the patterns of both parts into one Instrument.

The Gate, Note and Velocity Set libraries introduced by
`separate-velocity-set-gate-set-and-note-set` stay global. An Instrument does not own sets, and
any pattern may reference any set slot.

The feature also replaces the development "test" view (INIT view, label "tst", a bare number
counter) and the current function-key table with a hierarchy-oriented key map: Settings,
Instruments, Patterns, Sets.

The feature adds no musical parameters. A save made before this feature sounds the same after
it is loaded.

Decisions taken in this draft (each can be changed during review):

- A pattern can belong to several Instruments. (Chosen by the user.)
- MIDI channels are unique per Instrument. (Chosen by the user for now; may be revisited.)
- An Instrument has its own mute, separate from the per-pattern active flags. (Chosen by the
  user.)
- The pattern pool stays at `MAX_PATTERNS` = 15 in total.
- A pattern that belongs to no Instrument is allowed and is silent.
- F4, F5 and F6 open the Gate, Note and Velocity Set views. After a restart the UI starts in the
  Instruments view. (Chosen by the user.)
- A pattern can be switched off in two ways: for all Instruments with its active flag, or for
  one Instrument only with a per-route enable flag. (Chosen by the user.)

## Glossary

- **Instrument**: A group identified by its slot index, with one MIDI channel (1..16), one mute
  flag, and a Pattern Selection.
- **Instrument Slot**: One position, `0..MAX_INSTRUMENTS-1`, in the Instrument list. A slot is
  occupied or free.
- **Pattern Selection**: The set of patterns assigned to an Instrument, stored as a bitmask over
  the 15 patterns.
- **Route enable flag**: A per-Instrument, per-pattern flag, stored as a second bitmask over the
  15 patterns. It switches one pattern off in one Instrument only, without removing it from the
  Pattern Selection. Its value is meaningful only for patterns in the Pattern Selection.
- **Member Instrument**: An Instrument whose Pattern Selection contains a given pattern.
- **Pattern**: As defined in `separate-velocity-set-gate-set-and-note-set`, except that it has
  no MIDI channel of its own.
- **Active flag**: The existing per-pattern flag (`UIState::activePatterns`) that decides whether
  a pattern plays.
- **Instrument Mute**: A per-Instrument flag that silences that Instrument without changing the
  active flag of any pattern.
- **Audible route**: A pair (pattern, Instrument) where the pattern's active flag is set, the
  Instrument is a Member Instrument of the pattern, the route enable flag of the pair is set,
  and the Instrument is not muted. Each Audible route sends the pattern's notes on the
  Instrument's channel.
- **Working State**: The in-RAM state edited by the UI (`UIState`) and mirrored to core1.
- **Save Record**: The persisted configuration written by Ctrl+S (`persistence::` format).
- **Legacy Record**: A Save Record written before this feature, with a MIDI channel per pattern
  and no Instruments.

## Requirements

### Requirement 1: Instrument list

**User Story:** As a performer, I want to create Instruments, so that I can organise my patterns
into parts such as "bass", "lead" and "drums".

#### Acceptance Criteria

1. THE Working State SHALL hold an Instrument list with MAX_INSTRUMENTS = 16 Instrument Slots
   (0..15). Because channels are unique, this equals the number of MIDI channels.
2. THE Working State SHALL contain at least one occupied Instrument at all times.
3. WHEN the user creates an Instrument, THE Working State SHALL place it in the lowest free
   Instrument Slot with the lowest MIDI channel not used by another Instrument, not muted, and
   with an empty Pattern Selection.
4. IF all 16 Instrument Slots are occupied, THEN THE UI SHALL refuse the creation, show a
   "full" notice, and leave the Working State unchanged.
5. IF the user deletes the only occupied Instrument, THEN THE UI SHALL refuse the deletion,
   show a "last instrument" notice, and leave the Working State unchanged.
6. WHEN the user deletes an Instrument, THE Working State SHALL mark its slot free, SHALL NOT
   change any other Instrument, pattern, or pattern active flag, and THE Sequencer SHALL send
   Note Off for every note still sounding on that Instrument's channel from its patterns.
7. THE Working State SHALL accept deleting an Instrument whose Pattern Selection is not empty.
   Patterns that thereby lose their last Member Instrument become unassigned (Requirement 3.5).

### Requirement 2: Instrument MIDI channel

**User Story:** As a performer, I want to set the MIDI channel once per Instrument, so that all
of its patterns play on the same channel without editing each pattern.

#### Acceptance Criteria

1. THE Instrument SHALL hold exactly one MIDI channel in the range 1..16.
2. THE Working State SHALL NOT allow two occupied Instruments to hold the same MIDI channel.
3. WHEN the user steps an Instrument's channel up or down, THE UI SHALL move to the next channel
   in that direction that no other Instrument uses, SHALL NOT wrap past 1 or 16, and SHALL leave
   the channel unchanged when no free channel exists in that direction.
4. WHEN an Instrument's channel changes, every pattern of its Pattern Selection SHALL emit its
   MIDI messages on the new channel from its next note onward.
5. WHEN a channel change happens while a note sent for that Instrument is sounding, THE
   Sequencer SHALL send the Note Off for it on the channel on which its Note On was sent, so no
   note is left hanging.
6. THE Pattern SHALL NOT store a MIDI channel of its own.

### Requirement 3: Pattern selection per Instrument

**User Story:** As a performer, I want each Instrument to play a chosen set of patterns, and
I want to reuse one pattern in several Instruments, so that the same material can drive
several channels.

#### Acceptance Criteria

1. THE Instrument SHALL hold a Pattern Selection that contains any subset of the existing
   patterns, including none and all.
2. WHEN the user toggles a pattern in an Instrument's Pattern Selection, THE Working State
   SHALL add or remove only that pattern from that Instrument. Other Instruments, the
   pattern's set references, its active flag and its generation state SHALL remain unchanged.
3. THE Working State SHALL allow one pattern to be in the Pattern Selection of any number of
   Instruments, up to all 16.
4. WHEN a pattern is in the Pattern Selection of several Member Instruments, THE Sequencer
   SHALL generate one note per step for it, and SHALL send that note with the same pitch,
   velocity, and gate length on the channel of every Member Instrument that forms an Audible
   route. The pattern SHALL advance one step, not one step per Instrument.
5. WHERE a pattern has no Member Instrument, THE pattern SHALL stay in the Working State, keep
   its settings and active flag, and produce no MIDI output. THE UI SHALL show it as
   unassigned.
6. WHEN the user creates a pattern while an Instrument is selected, THE Working State SHALL add
   the new pattern to the Pattern Selection of the selected Instrument.
7. WHEN the user deletes a pattern, THE Working State SHALL remove it from every Instrument's
   Pattern Selection.
8. WHEN a toggle removes a pattern from an Instrument while a note sent for that route is
   sounding, THE Sequencer SHALL send the Note Off for it on the channel used for the Note On.
9. THE total number of patterns SHALL NOT exceed MAX_PATTERNS = 15.
10. WHEN a pattern is added to an Instrument's Pattern Selection, THE route enable flag of that
    pair SHALL be set. WHEN it is removed, THE flag SHALL be cleared.
11. WHEN the user disables the route of a pattern in an Instrument, THE pattern SHALL stay in
    that Instrument's Pattern Selection and THE Sequencer SHALL stop emitting new Note On
    messages for that pair on the Instrument's channel, applying the operation boundary
    (immediate, beat or bar) that a per-pattern deactivation uses today. Notes sounding for
    that pair SHALL get their Note Off on the channel used for the Note On.
12. WHEN the user disables a route, THE Working State SHALL NOT change the pattern's active
    flag, the same pattern's route in any other Instrument, any Instrument mute flag, or the
    pattern's generation state.
13. WHEN the user enables a route again, THE pattern SHALL resume on that Instrument's channel
    at the pattern's current step and SHALL NOT restart.
14. IF the user toggles the route enable flag of a pattern that is not in the Instrument's
    Pattern Selection, THEN THE UI SHALL ignore the request and leave the Working State
    unchanged.

### Requirement 4: Instrument Mute

**User Story:** As a performer, I want to mute an entire Instrument with one action, so that I
can drop a whole part out and bring it back with the same patterns playing as before.

#### Acceptance Criteria

1. THE Instrument SHALL hold a mute flag that defaults to not muted.
2. WHEN the user mutes an Instrument, THE Sequencer SHALL stop emitting new Note On messages on
   that Instrument's channel for all its patterns, applying the same operation boundary
   (immediate, beat or bar) that a per-pattern deactivation uses today.
3. WHEN an Instrument is muted while notes sent for it are sounding, THE Sequencer SHALL send
   the matching Note Off for each of them, so no note is left hanging.
4. WHEN a pattern is in the Pattern Selection of a muted Instrument and of an unmuted
   Instrument, THE pattern SHALL keep playing on the unmuted Instrument's channel only.
5. WHEN the user unmutes an Instrument, THE Sequencer SHALL resume the routes of exactly those
   patterns of the Instrument whose active flag is set.
6. THE Working State SHALL NOT change any pattern's active flag, and SHALL NOT change any other
   Instrument's mute flag, when an Instrument is muted or unmuted.
7. WHEN the user changes a pattern's active flag, THE new flag SHALL apply to the pattern in
   every Member Instrument. A pattern whose active flag is cleared SHALL be silent on all
   channels, regardless of Instrument mute flags and route enable flags. The active flag of
   a pattern SHALL be kept when its routes are enabled or disabled, and the route enable
   flags SHALL be kept when the active flag changes.
8. THE UI SHALL show a muted Instrument differently from an unmuted one, and SHALL show
   patterns that have at least one Audible route differently from patterns that have none.
9. THE three switches SHALL combine by AND: a pattern sounds on a channel only when the pattern
   is active, the route is enabled and the Instrument is not muted. Changing one switch SHALL
   NOT change the other two.

### Requirement 5: Shared set libraries

**User Story:** As a performer, I want the same gate, note and velocity sets to be usable from
every pattern and every Instrument, so that I can reuse a melody or rhythm anywhere.

#### Acceptance Criteria

1. THE Gate, Note and Velocity Set Libraries SHALL stay global and SHALL NOT be duplicated per
   Instrument.
2. THE Pattern SHALL keep exactly one Set Reference per set type, and any occupied Set Slot
   SHALL be selectable by any pattern, whichever Instruments select that pattern.
3. WHEN a set is edited, THE Working State SHALL apply the edit in place for every referencing
   pattern, as it does today.
4. WHEN the user deletes a set, THE UI SHALL refuse the deletion while any pattern references
   it, as it does today.
5. THE "go to pattern" action of each set view SHALL keep working with its current behaviour
   and SHALL list referencing patterns independent of Instruments.

### Requirement 6: Instruments view

**User Story:** As a performer, I want one screen for Instruments, so that I can see and change
channels, mutes, and which patterns each Instrument plays.

#### Acceptance Criteria

1. THE UIState SHALL hold a selected Instrument, which SHALL always be an occupied Instrument.
2. THE UI SHALL provide an Instruments view in which the user can move the selection between
   occupied Instruments, create and delete an Instrument, mute and unmute it, change its
   channel, and toggle patterns in its Pattern Selection.
3. THE Instruments view SHALL show for the selected Instrument its slot number, MIDI channel,
   mute state, and each of the 15 patterns in one of four states: not in the Pattern
   Selection, in the Pattern Selection with the route enabled and the pattern active (sounding),
   in the Pattern Selection with the route enabled but the pattern inactive, and in the Pattern
   Selection with the route disabled.
   3a. THE Instruments view SHALL let the user move a pattern cursor over the 15 patterns, add or
   remove the pattern under the cursor in the Pattern Selection, and enable or disable its
   route, each with its own key.
4. THE Instruments view SHALL show an overview of all 16 Instrument Slots (free, occupied,
   muted, selected).
5. THE UI SHALL render every new state on the 16x16 LED matrix without text, using colours and
   symbols consistent with the existing views.
6. THE new key bindings SHALL be assigned only to keys that no existing view uses in the same
   context. THE Instruments view SHALL be fully operable with the keyboard.
7. THE Patterns view SHALL keep editing patterns as it does today, SHALL mark patterns that are
   in the selected Instrument's Pattern Selection, and SHALL mark unassigned patterns.

### Requirement 7: Hierarchical function-key navigation

**User Story:** As a performer, I want the function keys to follow the structure of the
instrument (Settings, Instruments, Patterns, Sets), so that I can jump to any level of the
hierarchy with one key.

#### Acceptance Criteria

1. WHEN the user presses F1, THE UI SHALL open the Settings view.
2. WHEN the user presses F2, THE UI SHALL open the Instruments view.
3. WHEN the user presses F3, THE UI SHALL open the Patterns view.
4. WHEN the user presses F4, F5 or F6, THE UI SHALL open the Gate Set, Note Set or Velocity Set
   view, respectively, for the set the selected pattern references.
5. WHEN the user presses F7 to F12, THE UI SHALL consume the key without effect, as unbound
   function keys do today.
6. THE firmware SHALL NOT contain the INIT test view (the "tst" label and its value counter).
   THE `ViewId::INIT` entry SHALL be removed, and WHEN the device boots, THE UI SHALL start in
   the Instruments view with the selected Instrument being the lowest occupied slot.
7. WHEN any function key is pressed with a modifier, THE UI SHALL consume it without effect, as
   today.
8. THE host-control `H:view` command SHALL use the same numbering as the function keys
   (1 = Settings, 2 = Instruments, 3 = Patterns, 4..6 = sets), and the host-control view name
   report SHALL name the new views.
9. THE existing way of opening a set view from the Patterns view SHALL keep working.
10. THE BPM control SHALL stay available in the Settings view with its current key bindings.

### Requirement 8: Persistence

**User Story:** As a performer, I want Instruments saved with my project, so that my parts and
channels survive a power cycle.

#### Acceptance Criteria

1. THE Save Record SHALL store the Instrument occupancy and, per occupied Instrument, its MIDI
   channel, mute flag, Pattern Selection and route enable flags.
2. WHEN the user saves and the device later restores the record, THE Working State SHALL equal
   the saved state, including Instrument channels, mute flags, Pattern Selections and route enable flags.
3. THE Save Record SHALL NOT store a MIDI channel per pattern.
4. WHEN the device loads a Legacy Record, THE Working State SHALL create one Instrument per
   distinct pattern MIDI channel, in ascending channel order, SHALL put each pattern in the
   Pattern Selection of the Instrument with its former channel, SHALL set the route enable flag
   of every such pair, and SHALL set every Instrument to not muted. The resulting MIDI output SHALL equal the output the Legacy Record produced
   before this feature.
5. IF a loaded record has an occupied Instrument with a channel outside 1..16, two occupied
   Instruments with the same channel, a Pattern Selection or route enable mask containing a
   pattern index at or above the pattern count, a route enable flag set for a pattern outside the
   Pattern Selection, or no occupied Instrument, THEN THE device SHALL reject the record through
   the existing corrupt-record path and SHALL boot with default state.
6. THE save operation SHALL keep the existing transactional, MIDI-transparent behaviour and its
   timing limits. The larger record SHALL fit in the FRAM record area.

### Requirement 9: Real-time safety and cross-core behaviour

**User Story:** As a performer, I want Instrument edits to be as safe as pattern edits, so
that nothing hangs or glitches while the sequencer plays.

#### Acceptance Criteria

1. THE core1 sequencer SHALL receive each pattern's channel routes (the list of channels on
   which the pattern is audible) through the existing bounded command path, and SHALL NOT read
   UI state directly.
2. WHEN one edit changes the routes of several patterns (Instrument channel change, mute,
   unmute, delete, or pattern removal), THE UI SHALL deliver the change to core1 as one atomic
   command (or, where several commands are needed, inside one BATCH_BEGIN / BATCH_END frame) so
   core1 applies it at one tick.
3. THE Sequencer SHALL track note ownership per pattern, channel and note, so that a pattern
   routed to several channels releases each note on the channel it was sent on.
4. THE per-tick MIDI output of the worst case (15 patterns, each audible on 16 channels) SHALL
   be handled as specified in Requirement 11. It SHALL NOT overrun a queue, drop a Note Off, or
   leave a note hanging.
5. THE performance-safe-foundation timing gates (scheduler lateness, MIDI output bounds, save
   pause, fault latency) SHALL still pass for one Instrument and one pattern per Instrument
   at 40, 120 and 255 BPM.
6. THE golden MIDI stream test from `separate-velocity-set-gate-set-and-note-set` SHALL still
   produce the same bytes for a state with one Instrument holding the patterns that the golden
   state uses.
7. THE random number draws of a pattern SHALL NOT depend on the number of its Member
   Instruments, so a seeded pattern produces the same notes whether it is on one channel or on
   several.

### Requirement 11: MIDI output load

**User Story:** As a performer, I want a burst of notes at one tick to be spread over the
following milliseconds instead of breaking the rhythm, I want to see when the MIDI output is
getting too busy, and I want the device to stop cleanly when the load is more than the cable
can carry, so that I can judge how far I can push shared patterns.

Background (measured in task 11.3): MIDI bytes were written to the UART with a blocking write.
A burst of more than the 32-byte UART FIFO made core1 wait on the cable; a burst longer than one
tick made the scheduler skip ticks, which dropped their Note On and MIDI Clock bytes. The
one-beat timing fault never latched.

#### Acceptance Criteria

1. THE Sequencer SHALL NOT wait on the MIDI cable while producing MIDI bytes. Bytes SHALL be
   placed in a fixed-capacity output buffer (at least 4096 bytes) on core1 and moved to the UART
   by core1's main loop as the UART accepts them.
2. THE output buffer SHALL keep the byte order of all channel messages.
3. MIDI real-time bytes (Clock, Start, Continue, Stop) SHALL be sent ahead of buffered channel
   bytes, so that they are delayed by at most the bytes already handed to the UART (at most
   4 bytes, 1.28 ms).
4. IF the output buffer is full, THEN THE Sequencer SHALL wait for space (the previous blocking
   behaviour) and SHALL NOT drop any byte.
5. WHEN a burst of notes is produced at one tick and the average load fits the cable, THE
   Sequencer SHALL NOT skip ticks because of the burst.
6. WHEN the output buffer has been non-empty without interruption for one beat while playing,
   THE Sequencer SHALL enter the timing fault with cause MIDI output overload: stop the
   transport, release every sounding note and send MIDI Stop, as the existing timing fault does.
7. THE core1 timing fault state and its cause SHALL be published to core0. WHEN core1 latches
   a fault, THE UI SHALL set its fault flag and mark the transport as stopped, so that the next
   Space press is a Play that acknowledges the fault.
8. THE UI SHALL show a load indicator pixel at (15,4) in every view:
   - off: normal;
   - amber: within the last 2 seconds, a byte waited 10 ms or more in the output buffer;
   - red: a timing fault is latched (until the fault is acknowledged with Play).
9. THE load indicator SHALL be redrawn after every view render, save banner and view change,
   and no view SHALL draw anything else at (15,4).
10. THE golden MIDI stream test and the existing note lifecycle tests SHALL keep their output.

### Requirement 10: Documentation

**User Story:** As a customer, I want the manual to explain Instruments and the new keys, so
that I can use them without reading code.

#### Acceptance Criteria

1. THE user manual SHALL describe Instruments, the channel setting, Instrument mute, sharing a
   pattern between Instruments, the three switches (pattern active, route enable, Instrument
   mute) and how they combine, and what happens to an old save.
2. THE user manual SHALL describe the function-key map F1 to F6 and remove any reference to the
   test view.
3. THE user manual SHALL include LED-matrix screens generated from the real view code for the
   Instruments view and the changed Patterns view.
4. THE product charter's gap table SHALL mark per-pattern MIDI channel configuration as
   delivered through Instruments.
5. THE user manual SHALL describe the load indicator, the overload fault, how to recover from
   it, and the measured limits after Requirement 11.
