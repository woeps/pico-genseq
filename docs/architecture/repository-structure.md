# Repository Structure

```
pico-genseq/
├── src/
│   ├── genseq.cpp          # main(): starts the sequencer on core1, then the UI loop on core0
│   ├── common/             # SDK-free model shared by both cores (patterns, sets, scales, routing)
│   ├── commands/           # core0 → core1 command encoding over the multicore FIFO
│   ├── sequencer/          # core1: timeline, scheduler, notes, MIDI output, telemetry
│   ├── ui/                 # core0: input, UI state, views, LED matrix, host control
│   ├── persistence/        # save record codec, A/B record store, FRAM driver
│   ├── instrumentation/    # optional GPIO trace pins for timing measurements
│   ├── config/             # pins, TinyUSB config, binary info
│   ├── tools/              # optional bring-up / trace firmwares (FRAM)
│   └── generated/          # PIO headers
├── test/                   # host test project (RapidCheck property + example tests, ASan/UBSan)
│   └── bench/              # hardware bench scripts over the host-control channel
├── tests/                  # host test project for the UI (views, reducer, keyboard decoder)
├── tools/manual_screens/   # renders the user-manual LED screens from the real view code
├── docs/                   # documentation (user manual, architecture, development)
├── .kiro/specs/            # feature specs: requirements, design, tasks, hardware evidence
├── case/, data/            # enclosure designs, datasheets
├── CMakeLists.txt          # firmware build (Pico SDK)
├── ROADMAP.md, TODO.md
└── README.md
```

## `src/common/`

| File | Content |
|---|---|
| `pattern.*` | `Pattern`: gate, pitch and velocity set plus active flag and generation parameters (no MIDI channel) |
| `gate_set.*`, `pitch_set.*`, `velocity_set.*` | runtime sets with play orders (forwards, backwards, pendulum, random) |
| `GenerationParameters.h`, `Scale.*` | probability, scale, humanize, seed, coupling |
| `InstrumentRouting.h` | `RouteTable` (core1's Instrument routes) and `removePatternBit` |
| `MidiChannel.h`, `PatternId.h`, `TimeSignature.h` | canonical channel, stable pattern identity, meter |

## `src/sequencer/` (core1)

| File | Content |
|---|---|
| `sequencer.*`, `sequencer_task.cpp` | `Sequencer` and the core1 loop (receive command, update, pump MIDI) |
| `TransportTimeline.h`, `Scheduler.h`, `TimingPolicy.h` | 24-PPQN timeline, bounded tick servicing, late-tick reconciliation |
| `NoteLifecycle.h` | note ownership per (pattern, route), Note Off on the recorded channel |
| `MidiByteQueue.h`, `UartMidiOutput.*`, `IMidiOutput.h` | non-blocking MIDI output buffer and the UART sink |
| `PerformanceFault.h`, `PerformanceMetrics.h` | timing fault latch and metrics |
| `PatternSnapshot.h`, `CommandInbox.h` | allocation-free command receive |
| `sequencer_telemetry.h`, `stack_guard.h`, `core_pause.*` | cross-core telemetry, stack canaries, flash-write pause |

## `src/ui/` (core0)

```
ui/
├── ui.*, UIController.*     # main loop; owns the LED, the matrix, the views, saving, load pixel
├── Event.h, Types.h         # key events (pressed / released / held) and key ids
├── hardware/
│   ├── UsbKeyboard.*, KeyboardDecoder.*   # TinyUSB HID host → events
│   ├── LedMatrix.*, Led.*, driver/        # 16x16 WS2812 matrix (PIO + DMA), board LED
│   └── HostControl.*                      # "H:" bench command channel (host-control builds)
├── state/
│   ├── UIState.h            # the whole editable model plus view state
│   ├── Reducer.*            # global keys and every state action (sends commands)
│   ├── SetLibrary.*, Instruments.*        # pure model helpers
│   ├── PersistMapping.*     # UIState <-> PersistableConfig
│   └── LoadIndicator.h, FaultIndication.h # load pixel and fault policy
└── views/                   # one class per view (see UI System Guide)
```

`hardware/Display.*`, `hardware/Encoder.*` and `driver/LCD_I2C.*` are left over from the earlier
LCD/encoder prototype and are not used by the current firmware.

## Tests

- `test/`: `cmake -B test/build -S test -DGENSEQ_SANITIZE=ON && cmake --build test/build && ctest
  --test-dir test/build`. Example tests have their own `main()`; property tests run in one
  RapidCheck binary.
- `tests/`: `cmake -B tests/build -S tests && cmake --build tests/build && tests/build/genseq_tests`.
- Hardware: build with `-DGENSEQ_HOST_CONTROL=ON` and use the scripts in `test/bench/`.
