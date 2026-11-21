# Architecture Deep Dive

## Overview

GenSeq runs on an RP2040 and splits its work over the two cores:

| Core | Runs | Must never |
|---|---|---|
| core0 | USB keyboard host (TinyUSB), the UI state machine, the LED matrix, saving to the external FRAM, the host-control bench channel | write MIDI or touch sequencer state directly |
| core1 | the sequencer: transport timeline, patterns, note ownership, MIDI output | wait for the UI, allocate on the tick path, read `UIState` |

core0 owns the **model** (the whole setup the user edits, `ui::state::UIState`). core1 owns
**playback** (a runtime copy of exactly what it needs to play). The only way from core0 to core1
is the multicore FIFO command channel; core1 publishes a few read-only telemetry words back.

```
 core0                                                     core1
 ┌──────────────────────────────┐   multicore FIFO   ┌──────────────────────────────┐
 │ UsbKeyboard / HostControl    │   (commands)       │ CommandInbox / processCommand│
 │   → Event → reduce()         │ ─────────────────► │   → Sequencer                │
 │   → UIState (sets, patterns, │                    │      TransportTimeline       │
 │     Instruments)             │                    │      Scheduler (24 PPQN)     │
 │   → views → LedMatrix        │   telemetry words  │      Patterns + RouteTable   │
 │   → PersistenceManager/FRAM  │ ◄───────────────── │      NoteLifecycle           │
 └──────────────────────────────┘ (fault, load,      │      QueuedMidiOutput → UART1│
                                    sounding notes)  └──────────────────────────────┘
```

## Musical data model

```
Instrument (16 slots)              Pattern (up to 15)                Set libraries (16 slots each)
 channel 1..16 (unique)             active flag                       Gate Sets   (rhythm)
 muted                              gate / note / velocity reference ─► Note Sets   (pitches, scale)
 members  (pattern bitmask)  ──►                                      Velocity Sets (velocities,
 enabled  (route bitmask)                                                            humanize, seed)
```

- **Sets** live in three global libraries (`SetLibrary<…>` in `UIState.h`). A pattern holds one
  slot reference per library, so several patterns can share a set; editing a set changes every
  pattern that references it.
- **Instruments** (`InstrumentConfig`) hold one MIDI channel and two pattern bitmasks: `members`
  (assigned) and `enabled` (route switched on). A pattern may be a member of several Instruments
  and then plays the same notes on each channel.
- A note sounds on an Instrument's channel only if the pattern is active, its route is enabled
  and the Instrument is not muted. core0 folds `members & enabled & !muted` into one effective
  mask per Instrument before sending it to core1.
- Generation parameters (probability, scale, humanize, seed, coupling) are stored with the set
  that hosts them and reach core1 per pattern through `GEN_PARAMS_SET`.

The pure helpers that edit this model are in `src/ui/state/SetLibrary.*` and
`src/ui/state/Instruments.*`; the reducer (`Reducer.cpp`) calls them and sends the commands.

## Command system (`src/commands/command.h`)

Commands are packed into 32-bit FIFO words: a header word (`cmd | param1 << 8 | param2 << 16`)
followed by payload words for set data.

| Command | Payload | Purpose |
|---|---|---|
| `PLAY`, `STOP`, `BPM_SET` | — | transport |
| `PATTERN_ADD`, `PATTERN_REMOVE`, `PATTERN_ACTIVATE`, `PATTERN_DEACTIVATE` | pattern index | pattern list and active flag |
| `PATTERN_GATE_SET`, `PATTERN_PITCH_SET`, `PATTERN_VELOCITY_SET` | set data | material of one pattern |
| `*_SET_MULTI` | mask word + set data | one set applied to every pattern in a mask (shared sets) |
| `GEN_PARAMS_SET` | parameter block | generation parameters of one pattern |
| `BATCH_BEGIN`, `BATCH_END` | count | core1 holds ticks until a multi-command update has landed |
| `INSTRUMENT_SET` | mask word | replace one Instrument route slot: protocol channel + effective pattern mask |

core1 decodes into a reused fixed-size `PatternSnapshot`, so the receive path never allocates.
A command that names a missing pattern or an out-of-range value is rejected whole.

## Timing system (`src/sequencer/`)

- `TransportTimeline` is the single musical clock: 24 PPQN, tick duration from the BPM, meter for
  the beat length.
- `Scheduler::service()` runs every core1 loop. It advances every due tick up to a budget of 8 per
  call. A tick more than one tick period late is **reconciled**: pattern positions advance and due
  Note Offs are sent, but its Note Ons and its MIDI Clock byte are dropped. A tick one beat late
  latches the timing fault (`SCHEDULER_LATENESS`).
- On each tick `serviceTick()` walks the active patterns. On a rising gate edge the emission lens
  (`emitStep`) decides the note and velocity (probability, scale, humanize, coupling) and draws its
  random numbers once; the note is then sent on every route of the pattern
  (`RouteTable::routes[p]`, ascending slot order). A falling edge releases the pattern's voices.
- `NoteLifecycle` owns every sounding note as one voice per (pattern, route). Each Note Off goes to
  the channel its Note On used, a reference count per (channel, key) makes sure a key shared by two
  patterns gets one final Note Off, and a key already sounding is closed before it is struck again.
  Stop and every fault release all voices.

## MIDI output (`MidiByteQueue.h`, `UartMidiOutput.*`)

The sequencer writes bytes into `QueuedMidiOutput`, a 4096-byte ring plus a small real-time lane.
The core1 loop calls `pump()`: when the UART FIFO is empty it hands over up to 4 bytes, real-time
bytes (Clock, Start, Continue, Stop) first. This keeps core1 from ever waiting on the 31,250-baud
cable while it produces a burst of notes. A full ring waits rather than drop a byte.

- **Overload:** when the buffered bytes would leave the wire 250 ms or more late (782 bytes),
  `Sequencer::update()` latches the timing fault `MIDI_OUTPUT_OVERLOAD`.
- **Load indicator:** `g_midiLateMs` records when a byte last waited 10 ms or more; core0 shows it
  as the amber load pixel.

See [MIDI Implementation](../technical/midi-implementation.md) for the bytes on the wire.

## Timing fault

`PerformanceFault` latches the first fault (scheduler lateness or MIDI output overload): playback
stops, all notes are released, MIDI Stop is sent if clock output is on, and the working state is
kept. core1 publishes `g_timingFaultLatched` and `g_timingFaultCause`; `UIController` mirrors them
into `UIState` (fault flag, `playing = false`) and the load pixel turns red. Space sends Play,
which acknowledges the fault on core1.

## Persistence (`src/persistence/`)

Ctrl+S takes a `PersistableConfig` snapshot of `UIState` and writes a CRC-protected, versioned
record to an external MB85RS2MTA SPI FRAM through an A/B transactional `RecordStore`. Saving does
not pause playback (core1 keeps running). The current record is format v4 (set libraries,
pattern references, Instruments). v1 to v3 records still load: v1/v2 patterns become one set of
each kind per pattern, and the per-pattern MIDI channels of v1-v3 become one Instrument per
channel. An inconsistent record (bad references, duplicate channels, …) is rejected and the device
boots with defaults.

## UI architecture

See the [UI System Guide](../development/ui-system-guide.md).

## Memory and stacks

- Each core has a 2 KB stack: core1 `[0x20040800, 0x20041000)`, core0 `[0x20041800, 0x20042000)`,
  with an unused 2 KB gap in between. `stack_guard.h` places two canaries in the gap and core1
  checks them every loop.
- `UIState` and `PersistableConfig` are large (KB). Never copy them by value on core0: views and
  reducer functions mutate the state in place, and the save/load paths use static buffers.
- Never put a function-local static with a runtime constructor on the core1 path (its guarded
  lazy construction once hung core1); use file-scope objects.
- Steady-state ticking performs no heap allocation (covered by
  `test/example_steady_state_allocation.cpp`). Set replacement allocates, but only when a set
  command arrives.

## Telemetry and bench control

core1 publishes 32-bit words in `sequencer_telemetry.h` (sounding notes, fault, load, and in
host-control builds scheduler and wire-utilisation metrics). A firmware built with
`-DGENSEQ_HOST_CONTROL=ON` accepts `H:` commands on the UART0 console (`src/ui/hardware/HostControl.*`)
that synthesize key presses, dump the state (`H:dump`), capture the MIDI stream (`H:midilog`) and
reboot (`H:reset`), so hardware tests run without a keyboard. Scripts live in `test/bench/`.
