# MIDI Implementation

GenSeq has one serial DIN MIDI output. It sends Note On / Note Off on the channels of the
Instruments, plus MIDI Clock, Start and Stop. It sends no Control Change, Program Change, Pitch
Bend or SysEx, uses no running status, and has no MIDI input yet.

## Hardware

| Item | Value |
|---|---|
| UART | `uart1`, 31,250 baud, 8N1 (`MIDI_BAUD_RATE` in `UartMidiOutput.h`) |
| TX pin | GPIO 8 (`MIDI_UART_PIN_TX` in `src/config/pins.h`) |
| RX pin | GPIO 9, configured but unused |

One byte takes 320 us on the wire (10 bits), a three-byte Note On 960 us. The cable carries about
3,125 bytes per second.

## Messages sent

| Message | Bytes | When |
|---|---|---|
| Note On | `0x9n note velocity` (velocity 1-127) | rising gate edge of a pattern, once per route of the pattern |
| Note Off | `0x8n note 0` | falling gate edge, Stop, fault, a pattern unassigned / Instrument muted / channel changed, pattern removed or deactivated |
| Timing Clock | `0xF8` | every tick (24 per quarter note) while playing |
| Start | `0xFA` | Play |
| Stop | `0xFC` | Stop, and the timing fault |

`n` is the protocol channel 0-15 (user channel 1-16) of the Instrument the note is routed through.
`common::MidiChannel` is the one canonical representation; user channels are converted at the
boundary (`midi_channel::fromUserChannel`).

## From pattern to bytes

1. `Scheduler` runs a due tick; `Sequencer::serviceTick()` walks the active patterns.
2. On a rising edge `emitStep()` (the emission lens) turns the stream values into one note and
   velocity: step probability, scale quantize/skip, velocity humanize, coupling. It draws its random
   numbers once per step, so a seeded pattern repeats the same notes whatever its routes are.
3. The note goes to every route in `RouteTable::routes[pattern]` in ascending Instrument slot
   order, each on that Instrument's channel. A pattern without a route (not assigned, or all its
   Instruments muted) still advances and draws, so it stays in phase when it is assigned again.
4. `NoteLifecycle::noteOn(pattern, route, channel, note, velocity)` records the voice and writes the
   bytes. If the voice already holds a note, it is released first (monophonic per route). If another
   voice already sounds the same (channel, key), a Note Off is sent before the new Note On, so the
   wire always alternates On / Off per key. A reference count per (channel, key) sends the final
   Note Off only when the last owner releases it.
5. Each Note Off is sent on the channel recorded at Note On, also after the Instrument's channel
   changed.

## Output buffer

`MeteredMidiOutput` (byte counter, trace markers, bench MIDI tap) forwards every byte to
`QueuedMidiOutput<UartMidiOutput>` (`src/sequencer/MidiByteQueue.h`):

- Channel bytes go into a 4096-byte ring in order. Real-time bytes (`F8 FA FB FC`) go into a small
  separate lane.
- The core1 loop calls `pump()` before and after every `Sequencer::update()`. When the UART FIFO is
  empty it hands over up to 4 bytes, real-time lane first. MIDI allows a real-time byte between the
  bytes of any message, so a Clock byte is delayed by at most 4 bytes (1.28 ms) under load.
- A full ring makes `write()` pump until there is room; no byte is ever dropped.
- Measured on the board: at saturation the cable is 99.8% busy, and the longest gap between two
  pumps is 0.73 ms, shorter than the 1.28 ms a 4-byte refill takes to send. DMA would not add
  throughput.

## Load and overload

| Condition | Effect |
|---|---|
| a buffered byte waited 10 ms or more (31 bytes queued) | `g_midiLateMs` is updated; the UI shows the amber load pixel for 2 s |
| the buffered bytes would leave the wire 250 ms or more late (782 bytes) | timing fault `MIDI_OUTPUT_OVERLOAD`: stop, release all notes, send Stop; red load pixel; Space plays again |

Because every note of a tick is produced at once, a burst comes out as a short smear. Example from
the bench: 6 patterns playing every sixteenth note at 140 BPM, in every Instrument, run 30 s
without a fault on 10 Instruments (worst delay 89 ms) and stop with the overload fault on 12 and
16 Instruments.

## Clock and transport

- Clock output is on by default (`midiClockEnabled`). Play sends Start before the first Clock or
  note; Stop sends Stop only when the sequencer was playing; every tick sends one Clock byte.
- A tick that is more than one period late is reconciled: positions advance and due Note Offs go
  out, but its Note Ons and its Clock byte are not sent late. With the output buffer this no longer
  happens because of MIDI bursts; it can still happen if core1 is stalled.
- A tick that is one beat late latches the timing fault `SCHEDULER_LATENESS`.

## Testing and debugging

- Host tests: `test/example_note_lifecycle.cpp`, `test/property3_note_lifecycle_model.cpp`,
  `test/example_instrument_routing.cpp`, `test/example_midi_byte_queue.cpp`,
  `test/example_golden_stream.cpp` (a fingerprint of a fixed 3-pattern stream).
- Bench: a host-control build records every byte handed to the output (`H:midilog start` /
  `H:midilog dump`) and reports `soundingNotes`, `maxLatenessUs`, `missedTicks`, `wireBytes`,
  `maxBusyUs`, `peakBacklog`, `core1Fault` and `load` in `H:dump`. The tap records the order bytes
  were produced; real-time bytes may reach the wire slightly earlier.
- Timing traces: `-DGENSEQ_PERFORMANCE_TRACE_ENABLED=ON` with trace pins, see
  [Performance Instrumentation](../development/performance-instrumentation.md).
