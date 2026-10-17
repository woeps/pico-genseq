---
name: genseq-bitscope-measurement
description: Measure GenSeq (RP2040) real-time timing on hardware with a BitScope BS05U logic analyzer via scopething on Linux. Separates logical scheduling jitter from MIDI wire skew using the firmware's GPIO trace pins. Use for performance-safe-foundation task 1.4 and any later hardware timing gate (scheduler lateness, MIDI output bounds, save pause, fault latency).
triggers: bitscope, bs05u, logic analyzer, measure timing, hardware baseline, tick jitter, midi wire skew, save pause, trace pins, scopething, task 1.4, performance baseline, measure the pico
---

# GenSeq hardware timing measurement with a BitScope BS05U

The firmware already exposes the hard part: compile-time GPIO **trace pins** that
pulse on tick, MIDI dispatch, operation apply, save, and fault
(`src/instrumentation/PerformanceTrace.h`). Measuring is: build the instrumented
firmware, wire the BS05U to the trace pins + the real MIDI wire, capture under
defined load, and report **logical scheduling jitter** and **MIDI wire skew**
separately (the point of Req 11.7).

This skill is Linux + BS05U specific. The BS05U is a mixed-signal scope, not a
Saleae; **sigrok/PulseView do not support it**. The driver is
[scopething](https://github.com/jonathanhogg/scopething) (pure-Python BitScope
VM API). Captures are short triggered windows, not continuous streams — the
buffer is only 12,288 samples.

## 1. One-time setup

```bash
# scopething + deps into a scratch target (no venv needed if a pip is on hand)
git clone --depth 1 https://github.com/jonathanhogg/scopething "$KIROCREW_SCRATCH/scopething"
pip3 install --target "$KIROCREW_SCRATCH/pylibs" pyserial numpy   # scipy/pandas optional
export PYTHONPATH="$KIROCREW_SCRATCH/pylibs:$KIROCREW_SCRATCH/scopething"
```

The BS05U enumerates as an FTDI device (`0403:6001`) → `/dev/ttyUSB0`. It must be
openable by your user:

```bash
ls -l /dev/ttyUSB0                 # need rw; if nobody:nogroup, fix it:
sudo usermod -aG dialout $USER     # then re-login, OR one-off: sudo chmod 666 /dev/ttyUSB0
```

Handshake test (prints `Initialised scope, revision: BS000501`):

```bash
cd "$KIROCREW_SCRATCH/scopething"
PYTHONPATH="$KIROCREW_SCRATCH/pylibs:." python3 -c "import asyncio,scope; asyncio.run(scope.Scope().connect())"
```

## 2. Instrumented firmware

```bash
cmake -B build-trace -S . -G Ninja \
  -DGENSEQ_PERFORMANCE_TRACE_ENABLED=ON \
  -DGENSEQ_TRACE_TICK_PIN=2 -DGENSEQ_TRACE_MIDI_PIN=3 \
  -DGENSEQ_TRACE_OPERATION_PIN=4 -DGENSEQ_TRACE_SAVE_PIN=5 \
  -DGENSEQ_TRACE_FAULT_PIN=6
cmake --build build-trace
# flash build-trace/genseq.elf via OpenOCD (see project flash command), then POWER-CYCLE.
```

RP2040 note: OpenOCD's SYSRESETREQ leaves the chip in a bad state; **power-cycle
after flashing**, no SRST/watchdog tricks (known-issue in project memory).

## 3. Wiring (Pico powered off while connecting)

| BS05U logic | Pico GPIO | Signal |
|---|---|---|
| L0 | GP2 | Tick trace (core1) |
| L1 | GP3 | MIDI dispatch trace (core1) |
| L2 | GP4 | Operation trace (core1) |
| L3 | GP5 | Save trace (core0) |
| L4 | GP6 | Fault trace (level) |
| L5 | GP8 | **MIDI TX wire** (real UART) — for wire skew |
| GND | GND | common ground (**required**) |

BS05U L6/L7 are analog-comparator-derived; keep digital traces on L0–L5. Change
pins via the CMake flags above — never reuse GP8 (MIDI TX), GP16 (matrix),
GP25 (LED), GP9 (MIDI RX), or USB pins. Check `src/config/pins.h`.

## 4. Capture

Use the harness `docs/development/baseline/capture_baseline.py`:

```bash
cd "$KIROCREW_SCRATCH/scopething"   # so scope.py imports
export PYTHONPATH="$KIROCREW_SCRATCH/pylibs:$KIROCREW_SCRATCH/scopething"
python3 /path/to/repo/docs/development/baseline/capture_baseline.py grid       # tick period + jitter
python3 .../capture_baseline.py tickwidth  # tick service duration (hi-res)
python3 .../capture_baseline.py midi       # dispatch (L1) vs wire (L5)
python3 .../capture_baseline.py save        # Ctrl+S pause — arm, then press Ctrl+S on the device
```

The sequencer must be **playing with active gates** for MIDI (L1/L5) to pulse;
tick (L0) pulses whenever transport runs. Set BPM / pattern load from the device
front panel — the agent cannot drive the GenSeq UI.

## 5. Scenario matrix (Req 11.6)

Capture tick + wire at **BPM 40, 120, 255** under: (1) one pattern, (2) max
practical pattern load, (3) continuous editing, (4) Ctrl+S during playback,
(5) coincident pattern events.

## 6. Reading the two numbers (Req 11.7)

- **Logical Scheduling Jitter** — on L0/L1: deviation of actual edges from the
  ideal grid. Ideal tick period = `60e6 / (BPM * 24)` µs (24 PPQN). A spread of
  ≤ 1 sample period is **resolution-limited, not real jitter** — say so. Tick
  service duration (pin high-time) is the fixed per-tick logical cost (~5.5 µs on
  the baseline). MIDI dispatch (L1) is a non-blocking `uart_putc_raw` FIFO push,
  so its pulse is < 25 ns = effectively zero.
- **MIDI Wire Skew** — on L5 (GP8): single-bit low = 32.0 µs confirms 31,250
  baud; 1 byte = 10 bits = 320 µs; a 3-byte message ≈ 960 µs. Coincident notes
  serialize back-to-back, so N simultaneous messages skew the last by
  ~(N-1)·bytes·320 µs. Report wire skew as the serialization time, distinct from
  logical jitter.

## 7. BS05U gotchas (these WILL bite)

- **12,288-sample buffer** → span vs resolution trade off. Measure a slow period
  and a sub-µs pulse in **separate** captures.
- **Trigger-timeout register ≈ 419 ms max** (65535 × 6.4 µs). A triggered
  capture whose `(window + timeout)` wait exceeds that is refused with
  `Required trigger timeout too long`. Keep triggered windows short; for a rare
  edge, prefer free-run (no trigger) and find the edge in the data.
- **Pre-trigger unsupported above 5 MS/s** (trigger_position forced to 0).
- **Valid logic modes**: `LogicShot` 40–13 MS/s, `LogicFast` 10 MS/s, `Logic`
  8 MS/s–2.4 kS/s. Pick the sample period as an integer count of 25 ns primary
  ticks inside one mode's clock range, or you get
  `Unable to find appropriate capture mode`.
- **Ultra-short pulses** (the < 25 ns dispatch pulse) are not reliably latched by
  the hardware trigger comparator — free-run instead, or treat as below-floor.
- Do **not** reconnect in a tight loop: every `connect()` resets the scope;
  hammering it wedges the FTDI port. One connection per capture session.

## 8. Evidence to retain (Req 9.3 / Milestone 2 gate)

Firmware commit, CMake trace flags, analyzer config (rate/window/trigger per
scenario), raw captures, and observed maxima — all under
`docs/development/baseline/`. The baseline write-up is
`docs/development/baseline/README.md`.
