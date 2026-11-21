# Implementation Plan

Each task ends with the host suite green (ASan+UBSan clean) and the firmware linking. Commit
after each numbered task. Requirement and design references are in brackets. Tasks marked `*`
are tests. Steps that need the board are marked HW and stop for the user. Files that use CRLF
line endings keep CRLF after editing.

- [x] 1. Baseline and call-site audit
  - [x] 1.1 Confirm the golden MIDI stream test (hash `0x6e772345f8af0f79`, 1304 bytes) passes
    on the current master and note the command that runs it in this file's commit message.
    [R9.6]
  - [x] 1.2 List every use of `Pattern::midiChannel`, `getMidiChannel`, `setMidiChannel`,
    `getCanonicalChannel`, `PatternConfig::midiChannel`, `ViewId::INIT`, `InitView`,
    `UIState::value`/`setValue` and `H:view` (`grep -rn` over `src`, `test`, `tests`, `tools`,
    `docs`) and record the list in design.md under a new "Call-site audit" heading. [Design 4, 7]
  - [x] 1.3 Record the core1 stack-guard margin and the `.bss` size from the current release
    link map in design.md (used by 7.5). [Design 8]

- [x] 2. Shared routing model (SDK-free)
  - [x] 2.1 Create `src/common/InstrumentRouting.h` with `MAX_INSTRUMENTS`,
    `MAX_ROUTE_PATTERNS`, `removePatternBit` and `RouteTable` (`reset`, `set`, `removePattern`,
    transposed `routes[]`). [R1.1, Design 1]
  - [x]* 2.2 Host tests `test/example_route_table.cpp`: `reset` default, `set` keeps `routes[]`
    consistent with `patterns[]`, `removePatternBit` against a brute-force reference for every
    mask and index, `removePattern` equals `removePatternBit` on every slot.
  - [x] 2.3 Register the test in `test/CMakeLists.txt`.

- [x] 3. NoteLifecycle voices per (pattern, route)
  - [x] 3.1 Change ownership to `owned_[PATTERN_CAPACITY][MAX_INSTRUMENTS]`; add
    `noteOn(id, route, channel, note, velocity)`, `releaseRoute(id, route)`; keep the four-argument
    `noteOn` as route 0; make `releasePattern`, `releaseAll`, `panic`, `ownershipEmpty` and
    `soundingCount` cover all voices. Keep the retrigger Note Off rule. [R9.3, Design 8]
  - [x]* 3.2 Extend `test/example_note_lifecycle.cpp` and `test/example_note_balance.cpp`:
    one pattern on three routes sounds and releases on three channels; `releaseRoute` releases
    only its voice; a monophonic replacement on one route does not touch the others; shared key
    on two patterns still gives one final Note Off.
  - [x]* 3.3 Extend `test/property3_note_lifecycle_model.cpp`: random sequences of noteOn,
    releaseRoute, releasePattern, channel change and `releaseAll`; the wire balance is zero for
    every (channel, note) after a final `releaseAll`.

- [x] 4. Remove the per-pattern channel from the engine
  - [x] 4.1 Remove `midiChannel`, its accessors, `getCanonicalChannel` and the constructor
    parameter from `src/common/pattern.h/.cpp`. [R2.6]
  - [x] 4.2 Add `common::RouteTable routeTable` to `Sequencer`; initialise with `reset()` in
    `init()`; compact it in `removePattern`. [Design 7.2]
  - [x] 4.3 Split `emitStep` into the generation step (returns result, note, velocity, consumes
    the same random draws as today) and the wire step; in `serviceTick` call the generation step
    for every active pattern with non-empty material, then fan the note out over
    `routeTable.routes[i]` in ascending slot order. A pattern without a route still draws and
    advances. [R3.4, R3.5, R9.7, Design 7.3]
  - [x] 4.4 Update the existing host tests that set a channel on a pattern
    (`example_canonical_channel`, `example_sequencer_integration`, `property2`, `property5`,
    `example_pattern_snapshot_apply`, `smoke_host_compilation` and the others found in 1.2) so
    they drive channels through the route table. [Testing 8]
  - [x]* 4.5 Golden stream check: the default single route must give the unchanged hash.
    [R9.6]

- [x] 5. `INSTRUMENT_SET` command and core1 application
  - [x] 5.1 Append `Command::INSTRUMENT_SET` and `sendInstrumentSet(slot, protocolChannel, mask)`
    to `src/commands/command.h/.cpp`; replace `isMultiSetCommand` use in the receive paths with
    `hasMaskWord(cmd)` and read the mask word for it in `receiveCommand` and
    `receiveCommandInto` without allocation. [R9.1, Design 6]
  - [x] 5.2 Add `Sequencer::applyInstrument(slot, channel, mask)` (public for tests): release
    the voices of patterns that left the mask, release all voices of the slot when the channel
    changes, then `routeTable.set`. Route it from both `processCommand` overloads and from
    `canApplyCommand` (slot < 16, channel <= 15, mask within `patterns.size()`). [R2.4, R2.5,
    R3.8, R4.2, R4.3, Design 7.1]
  - [x]* 5.3 Host tests `test/example_instrument_routing.cpp`: fan-out on three channels in
    ascending slot order; route removal, mute (mask 0) and channel change each emit the Note Off
    on the recorded channel; malformed commands are rejected without change; a seeded pattern
    produces the same notes with 0, 1 and 3 routes; `PATTERN_REMOVE` compacts the table.
    [R9.4, R9.7]
  - [x]* 5.4 Extend `test/example_steady_state_allocation.cpp` to cover `INSTRUMENT_SET`.
    [Design 6]

- [x] 6. UI state model and reducer
  - [x] 6.1 Add `MAX_INSTRUMENTS`, `InstrumentConfig`, `InstrumentNotice` and the instrument
    fields to `src/ui/state/UIState.h` with the default slot 0 (`{1, false, 0x0001, 0x0001}`).
    Add `validateInstruments()` for tests. [R1.1, R1.2, Design 2]
  - [x] 6.2 Create `src/ui/state/Instruments.h/.cpp` (SDK-free where possible, commands behind
    the existing stub in host builds) with `createInstrument`, `deleteInstrument`,
    `stepInstrumentChannel`, `toggleInstrumentMute`, `toggleMember`, `toggleRoute`,
    `selectInstrument`, `moveInstrumentCursor`, `syncInstrument`. [R1.3-1.7, R2.1-2.4, R3.1-3.3,
    R3.10-3.14, R4.1-4.6, R6.1, R6.2]
  - [x] 6.3 Hook `addPattern` (add to the selected Instrument and sync), `removePattern`
    (`removePatternBit` on every Instrument), and `restoreSequencerFromState` (send
    `INSTRUMENT_SET` for all 16 slots). `togglePatternActive` stays unchanged. [R3.6, R3.7,
    R4.7, Design 3]
  - [x]* 6.4 Reducer host tests in `tests/`: notices (full, last, no free channel, not a member),
    lowest free slot and channel, channel stepping skips used channels and stops at 1 and 16,
    member and route toggles keep the invariants, add and remove pattern update masks, and a
    random operation sequence keeps `validateInstruments()` true. A recording `commands` stub
    checks that each edit sends exactly one `INSTRUMENT_SET` with the folded mask
    (`members & enabled`, or 0 when muted). [R4.9, Design 3]
  - [x]* 6.5 Cross-check test: apply the same random pattern removals to the reducer masks and to
    a `RouteTable` and assert equality. [Risks]

- [x] 7. Function keys, boot view and removal of the test view
  - [x] 7.1 Replace `ViewId::INIT` with `ViewId::INSTRUMENTS` in `UIState.h` (order: SETTINGS,
    INSTRUMENTS, PATTERNS, GATE_SET, PITCH_SET, VELOCITY_SET); set the default `currentView`
    to INSTRUMENTS; update `FUNCTION_KEY_VIEWS` and its comment. [R7.1-7.6]
  - [x] 7.2 Factor the Enter path of `PatternsView` into
    `openSetViewForSelectedPattern(state, SetType)` in the reducer; bind F4-F6 to it as global
    keys (no effect without a selected pattern; no restart when already in that set view).
    [R7.4, R7.9, Design 4]
  - [x] 7.3 Delete `InitView.h/.cpp` and its entries in `tests/CMakeLists.txt` and the firmware
    CMake; move the play LED blink/off logic from `InitView::render` into `UIController`'s render
    dispatch; remove `UIState::value`/`setValue` if unused. [R7.6, Design 4]
  - [x] 7.4 Update `HostControl`: `H:view` accepts 1..6, `viewName` names `instruments` and drops
    `init`, and the usage comment; add `D:ins=<slot>:<ch>:<m>:<members>:<enabled>` lines to the
    dump; fix every script found in 1.2 that used the old `H:view 1`. [R7.8, Design 4]
  - [x]* 7.5 Host tests: F1-F6 select the views, F7-F12 and modified F-keys are swallowed, the
    boot view is INSTRUMENTS, F4 and Enter in the Patterns view produce the same state. (The play
    LED lives in the firmware-only `UIController`; it is verified on hardware in 11.1 instead of
    with a host fake.) Update the expectations on `ViewId::INIT` in
    `tests/test_reducer.cpp` and `tests/test_patternsview.cpp`. [R7]
  - [x] 7.6 Build the firmware, read the link map and record the core1 stack-guard margin and
    `.bss` growth against the values from 1.3. Stop and report if the margin falls below the
    documented guard. [Risks]

- [x] 8. Instruments view
  - [x] 8.1 Create `src/ui/views/InstrumentsView.h/.cpp`: key handling from the design table
    (arrows, Ctrl+arrows, Enter, E, M, Shift+arrows, Ctrl+N, Delete) calling the reducer
    functions; register it in `UIController` and the firmware and host CMake files. [R6.2,
    R6.6, Design 5]
  - [x] 8.2 Render: label and channel number, muted tint, 15 pattern cells in the four states
    plus not-assigned, blinking cursor, 16-pixel Instrument strip, notice flash. [R4.8, R6.3,
    R6.4, R6.5]
  - [x] 8.3 Update `PatternsView::render` with the member / non-member / unassigned markers.
    [R6.7]
  - [x]* 8.4 Host tests with a fake `LedMatrix`: each cell state produces its expected pixels, the
    strip shows free/occupied/muted/selected, the Patterns view markers, and every key reaches the
    intended reducer function. [R6]

- [x] 9. Persistence format v4
  - [x] 9.1 In `PersistableConfig.h` remove `PatternConfig::midiChannel`, add `InstrumentRecord`,
    `instrumentOccupied` and `instruments[16]` with the default slot 0; extend `operator==` for
    occupied slots. [R8.1, R8.3, Design 9]
  - [x] 9.2 In `SaveRecord.cpp` set `FORMAT_VERSION = 4`, write the v4 layout, and add the v4
    decode with the validation rules (channel range, unique channels, masks within the pattern
    count, `enabled` a subset of `members`, at least one Instrument). Failures use the existing
    corrupt-record result. [R8.1, R8.5]
  - [x] 9.3 Add `deriveInstrumentsFromChannels` and call it from the v1, v2 and v3 decode paths
    (collect the per-pattern channels into a local array first). [R8.4]
  - [x] 9.4 Update `PersistMapping.cpp` (in place, no by-value config) and `fram_save_trace.cpp`.
    Reset `selectedInstrument` and `instrumentCursor` on load. [R8.2]
  - [x]* 9.5 Extend `PersistGen.h`, `PersistTestSupport.h` and `property1_persist_roundtrip.cpp`
    to generate Instruments; add examples: v1/v2/v3 fixtures decode to one Instrument per channel
    with all routes enabled and nothing muted; the MIDI stream of a migrated record equals the
    stream of the same state described by channels; each corrupt variant is rejected and boots
    defaults; the worst-case v4 record fits half of the FRAM region. Update
    `example_header_layout`, `example_outcome_mapping`, `example_banner_mapping`,
    `example_set_persistence` and `property7_genparams_roundtrip` for the format change.
    [R8.4-8.6]
  - [x] 9.6 Confirm the save path timing code needs no change: the instrument block is inside the
    same transactional record. [R8.6]

- [x] 10. Host suite and firmware build checkpoint
  - [x] 10.1 Run the full `test/` and `tests/` suites under ASan and UBSan and fix failures.
  - [x] 10.2 Build the release firmware (`build/release/`) and confirm it links.
  - [x] 10.3 Commit; the next step needs the board.

- [ ] 11. Hardware verification (HW)
  - [x] 11.1 Flash the release build; verify boot into the Instruments view with the
    `D:view` and `D:ins=` dump, and MIDI of one Instrument against the pre-feature behaviour.
    [R7.6, R9.6]
  - [ ] 11.2 Ask the user to power-cycle the board; with host-control keys build two Instruments
    sharing one pattern, mute one, disable one route, save, power-cycle, and compare the
    `D:ins=` dump and a MIDI capture before and after. [R8.2, R4, R3.11]
  - [x] 11.3 Fan-out stress: several patterns on several Instruments while editing masks, channels
    and mutes; capture MIDI and check for unbalanced Note On/Off, and read the fault and lateness
    counters over host control. Record where the MIDI bandwidth limit shows up, and add the measured limit to the
    "MIDI bandwidth" paragraph of section 4a of the manual. [R9.4]
  - [x] 11.4 Run the performance-safe-foundation timing gates (scheduler lateness, MIDI output
    bounds, save pause, fault latency) with one Instrument at 40, 120 and 255 BPM using the
    BitScope skill; compare with the baseline in `docs/development/baseline/README.md`. [R9.5]
  - [x] 11.5 Read the core1 stack-guard margin on the board. [Risks]

- [x] 12. Documentation
  - [x] 12.1 Generate the LED screens for the Instruments view (each cell state, muted tint,
    strip) and the changed Patterns view using the `genseq-led-matrix-doc-screenshots` skill.
    [R10.3]
  - [x] 12.2 Update `docs/user/user-manual.md`: Instruments, channel setting, mute, sharing a
    pattern, the three switches and how they combine, the F1-F6 map, the removal of the test
    view, the v4 save and the bandwidth note from 11.3. [R10.1, R10.2]
  - [x] 12.3 Update the charter gap table: per-pattern MIDI channels delivered through
    Instruments. Update `ROADMAP.md` and `TODO.md` where they mention per-pattern channels or the
    test view. [R10.4]

- [ ] 13. Final acceptance
  - [ ] 13.1 Walk the requirements list R1-R10 against the tests and the HW results and record any
    gap in this file.
  - [ ] 13.2 Run the full host suite and the firmware build once more, then open a pull request
    from the feature branch with the evidence from 11.
