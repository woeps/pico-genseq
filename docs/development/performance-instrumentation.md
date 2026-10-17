# Performance Instrumentation

Milestone 2 provides fixed-size sequencer metrics and optional GPIO traces for repeatable timing
measurements. Instrumentation is disabled by default and no trace pin is assigned unless it is
explicitly configured.

## Runtime metrics

`sequencer::PerformanceMetrics` currently records:

- logical ticks serviced
- maximum scheduler lateness in microseconds
- ticks serviced at least one full tick period late
- reconciliation count (used by the later scheduler work)
- MIDI bytes dispatched
- operations applied
- MIDI and command queue high-water marks (used by the later bounded queues)
- latest performance fault cause

The metrics contain no dynamic allocation. `Sequencer::performanceMetrics()` exposes a
read-only snapshot and `resetPerformanceMetrics()` clears the counters.

## GPIO trace configuration

Enable traces through CMake cache settings. All pins default to `-1`, which disables that
individual signal even when tracing is enabled.

```bash
cmake -B build -S . -G Ninja \
  -DGENSEQ_PERFORMANCE_TRACE_ENABLED=ON \
  -DGENSEQ_TRACE_TICK_PIN=2 \
  -DGENSEQ_TRACE_MIDI_PIN=3 \
  -DGENSEQ_TRACE_OPERATION_PIN=4 \
  -DGENSEQ_TRACE_SAVE_PIN=5 \
  -DGENSEQ_TRACE_FAULT_PIN=6
cmake --build build
```

These GPIO numbers are examples only. Check the actual wiring and `src/config/pins.h` before
selecting pins. Do not reuse MIDI TX GPIO8, LED matrix GPIO16, the onboard LED GPIO25, USB pins,
or any other connected peripheral pin.

To return to a normal build:

```bash
cmake -B build -S . -DGENSEQ_PERFORMANCE_TRACE_ENABLED=OFF
cmake --build build
```

## Signal meanings

| Trace | High interval |
|---|---|
| Tick | Production processing of one due logical tick |
| MIDI | One call into the injected MIDI byte output |
| Operation | Application of one non-NOOP sequencer command |
| Save | Persistence manager save call on core0 |
| Fault | Latched performance fault; wired by the later fault-state task |

The tick, MIDI, and operation traces run on core1. Save runs on core0. Fault is a level rather
than a pulse so a logic analyzer can correlate the point at which output is stopped.

## Baseline capture

Before changing note ownership or scheduler behavior, capture the existing implementation at
BPM 40, 120, and 255 under:

1. one active Pattern
2. the maximum practical Pattern load
3. continuous keyboard editing
4. Ctrl+S during playback
5. coincident Pattern events

Record MIDI TX GPIO8 alongside the configured trace signals. Report logical dispatch timing
separately from the unavoidable 31,250-baud MIDI wire serialization time. Preserve analyzer
configuration, firmware commit, test configuration, captures, and observed maxima as Milestone
2 evidence.
