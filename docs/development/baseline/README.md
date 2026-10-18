# Hardware Timing Baseline (Milestone 2, Task 1.4)

Captured baseline of the **current** GenSeq implementation before note-ownership
and scheduler changes, per
[`performance-safe-foundation` task 1.4](../../../.kiro/specs/performance-safe-foundation/tasks.md)
and Requirements 9.2, 9.3, 11.6, 11.7.

The point of this task is to separate two things the spec insists on reporting
independently (Req 11.7):

- **Logical Scheduling Jitter** — deviation of an event's actual dispatch from
  its intended scheduler timestamp, excluding wire time. Measured on the
  firmware trace pins.
- **MIDI Wire Skew** — the unavoidable delay from serializing bytes at 31,250
  baud. Measured on the real UART TX line.

## Evidence provenance

| Item | Value |
|---|---|
| Firmware commit | `b94a627` (`feat: add performance timing instrumentation`) |
| Firmware image | `build-trace/genseq.elf`, built with the trace CMake flags below |
| Analyzer | BitScope Micro **BS05U**, rev `BS000501`, 40 MHz primary clock, 12,288-sample buffer, 3.3 V logic |
| Analyzer host | Linux, `/dev/ttyUSB0` (FTDI `0403:6001`) |
| Capture driver | [scopething](https://github.com/jonathanhogg/scopething) (pure-Python BitScope VM API) |
| Capture harness | [`capture_baseline.py`](capture_baseline.py) (in this directory) |
| Raw captures | [`captures/*.npy`](captures/) (1 bit/sample logic arrays) |

### Trace build (CMake cache)

```
GENSEQ_PERFORMANCE_TRACE_ENABLED = ON
GENSEQ_TRACE_TICK_PIN      = 2   # GP2  (core1) one due logical tick serviced
GENSEQ_TRACE_MIDI_PIN      = 3   # GP3  (core1) one byte written into MIDI output
GENSEQ_TRACE_OPERATION_PIN = 4   # GP4  (core1) one non-NOOP command applied
GENSEQ_TRACE_SAVE_PIN      = 5   # GP5  (core0) persistence save call
GENSEQ_TRACE_FAULT_PIN     = 6   # GP6  latched performance fault (level)
```

Build / flash:

```bash
cmake -B build-trace -S . -G Ninja \
  -DGENSEQ_PERFORMANCE_TRACE_ENABLED=ON \
  -DGENSEQ_TRACE_TICK_PIN=2 -DGENSEQ_TRACE_MIDI_PIN=3 \
  -DGENSEQ_TRACE_OPERATION_PIN=4 -DGENSEQ_TRACE_SAVE_PIN=5 \
  -DGENSEQ_TRACE_FAULT_PIN=6
cmake --build build-trace
# flash build-trace/genseq.elf via OpenOCD, then POWER-CYCLE the Pico
```

### Analyzer wiring

| BS05U logic | Pico GPIO | Signal |
|---|---|---|
| L0 | GP2 | Tick trace |
| L1 | GP3 | MIDI dispatch trace |
| L2 | GP4 | Operation trace |
| L3 | GP5 | Save trace |
| L4 | GP6 | Fault trace (level) |
| L5 | GP8 | **MIDI TX wire** (real UART line) |
| GND | GND | common ground (required) |

(BS05U L6/L7 are derived from analog comparators; keep all digital traces on
L0–L5.)

## Results

### Tempo sweep, single pattern (Req 11.6)

| BPM | Ideal tick period | Measured period | Implied BPM | Logical jitter | Capture |
|---|---|---|---|---|---|
| 40  | 62,500 µs | 62,500 µs (std 0.0) | 40.00  | 0, below floor | `captures/tick_L0_40bpm.npy` |
| 120 | 20,833 µs | 20,833 µs (±1 sample) | 120.00 | ≤ resolution | `captures/tick_L0_120bpm.npy` |
| 255 | 9,804 µs  | 9,803 µs (±1 sample)  | 255.01 | ≤ resolution | `captures/tick_L0_255bpm.npy` |

Every measured deviation equals exactly one analyzer sample period
(quantization), not real jitter. At each tempo the mean lands on the ideal grid.

- **Tick service duration (fixed logical cost):** **5.5 µs** at 250 ns
  resolution (`captures/tick_service_hires.npy`). This is how long core1 holds
  the Tick pin high to process one due tick — negligible against even the 9.8 ms
  tick budget at 255 BPM.
- **MIDI dispatch pulse (L1):** **below the 25 ns analyzer floor.**
  `UartMidiOutput::write` is `uart_putc_raw` — a non-blocking push into the UART
  FIFO — so the logical cost of dispatching a MIDI byte is effectively zero.

### MIDI wire (Req 11.7, wire skew)

Measured on L5 (GP8), the real UART line:

- **Single-bit low time = 32.0 µs** → confirms 31,250 baud (1/31250 = 32 µs).
- **One byte = 10 bits = 320 µs** on the wire (1 start + 8 data + 1 stop).
- A 3-byte Note On / Note Off occupies up to **~960 µs** of wire time.
- Captures: `captures/wire_L5_{40,120,255}bpm.npy` (baud is tempo-independent).

### Load: 8 patterns @ 120 BPM

> **Note (corrected during task 9.2):** the live-load captures in this task-1.4
> section were taken with **8** patterns active. The later 9.2 captures (below)
> use **10** patterns — the true worst-case load the device supports. Pattern
> count affects only wire-burst length, not scheduler jitter, so the task-1.4
> conclusions stand; the 9.2 section is the authoritative worst-case load.

| Metric | Value | Capture |
|---|---|---|
| Tick period | 20,834 µs (±1 sample) → 120.00 BPM | `captures/tick_L0_8pat_120bpm.npy` |
| Logical jitter | **0** (grid still exact under 8× load) | — |
| Coincident-note wire burst | **~2,240 µs contiguous**, bytes back-to-back (320 µs apart), UART saturated | `captures/wire_L5_8pat_120bpm.npy` |

8 patterns add **no scheduler jitter**. The only load effect is on the wire:
coincident notes serialize into a ~2.2 ms burst, so the last coincident note is
skewed ~2.2 ms behind the first — entirely wire-bound (dispatch is < 25 ns).

### 🔴 Ctrl+S save during playback (Req 9.2, 9.3) — the Milestone 2 defect

Captured with Save (L3) + Tick (L0) + Wire (L5) together, triggered on Save
rising (`captures/save_full_120bpm.npy`, 120 BPM):

| Measurement | Value | Req 9.3 budget | Verdict |
|---|---|---|---|
| **Save pause (full, contiguous)** | **32.63 ms** | — | one flash erase+program |
| MIDI wire activity during save | **0.00%** (silent) | ≤ 1 ms MIDI interruption | **🔴 ~32× over budget** |
| Ticks serviced during save | 1 (expected ~1.6) | — | **scheduler parked** |

`UIController` runs `PersistenceManager::save()` synchronously: it parks core1
and performs a blocking internal-flash erase+program on core0. During the write,
XIP is disabled, so **both cores stall and MIDI output ceases for ~33 ms** — over
a full beat at 120 BPM, at the one-beat fault threshold. This is the empirical
justification for Milestone 2's transparent-save work (RAM-resident execution
path, or an external nonvolatile backend per Req 9.4).

---

# Task 9.2 — Internal flash save under worst-case live load

Task 1.4 (above) established the save pause with a single pattern on the
**pre-Milestone-2** firmware (`b94a627`). Task 9.2 re-measures the save under
**worst-case live load on the current `agents` branch**, which has the hardened
fail-closed pause protocol from task 9.1, and answers the two questions the 1.4
baseline could not: *(a)* does the hardened pause still freeze both cores, and
*(b)* does the freeze endanger a note that is sounding when the save lands
(stuck-note / note-duration safety for Req 9.2 and later 12.4)?

### Evidence provenance (9.2)

| Item | Value |
|---|---|
| Firmware commit | `9f992fb` (`agents` branch, all host-implementable Milestone 2 work) — includes the task-9.1 hardened `core_pause` + fail-closed `FlashStore` |
| Firmware image | `build-trace/genseq.elf` rebuilt on `agents` with the trace CMake flags |
| Live load | **10 patterns active with gates on, transport running, MIDI wire saturated** |
| Save trigger | Ctrl+S from a USB keyboard on the GenSeq host port (`Reducer::isSaveCombo`, global) |
| Analyzer / driver | same BS05U + scopething as task 1.4 |
| Capture harness | `capture_coincident_save.py` (hardware trigger on L3, dict trigger `{3:1}`) |
| Raw captures | `captures/save_live_120bpm_10pat.npy`, `captures/save_live_255bpm_10pat.npy`, `captures/save_coincident_255bpm_10pat.npy` |

### Results — freeze across tempos

| Metric | 120 BPM | 255 BPM | Req 9.3 budget | Verdict |
|---|---|---|---|---|
| Save pause (contiguous) | 34.94 ms | 37.03 ms | — | one flash erase+program+verify |
| Beat length | 500 ms | 235 ms | — | — |
| Save as % of one beat | 7.0% | **15.8%** | — | — |
| Ticks serviced during save | 1\* (exp. 1.68) | **0** (exp. 3.78) | — | 🔴 scheduler parked |
| MIDI wire active during save | 5.66%\* | **0.0%** | ≤ 1 ms interruption | 🔴 **~37× over** |

\* The 120 BPM capture used a free-run window that happened to include the
release-boundary tick/wire resume. The 255 BPM capture is hardware-triggered on
L3 at 5 µs resolution with the pulse fully contained, and shows the interior
cleanly: **zero ticks, zero MIDI bits for the entire 37 ms.**

**RAM-resident path audit:** the task-9.1-hardened save path still freezes both
cores for ~37 ms. The RAM-resident `doFlashWrite` + `corePauseSpin` correctly
prevent an XIP-down crash, but by design they *park* core1 rather than keep it
sequencing — so during the write there is **no tick advance and no MIDI
dispatch at all**. Sequencing does **not** continue while XIP is unavailable.

### Results — coincident-burst (note-safety)

Capture: `captures/save_coincident_255bpm_10pat.npy`, 255 BPM, 10 patterns,
Ctrl+S landing while patterns fire together.

| Measurement | Value | Meaning |
|---|---|---|
| Save pause | 35.73 ms | total freeze |
| Ticks / wire edges during save | 0 / 0 | both cores dark |
| Last wire activity **before** save | −0.85 ms | a note byte was on the wire as the save began |
| First wire activity **after** release | +0.09 ms | wire resumes immediately on release |
| **Wire silence gap** | **36.67 ms** | a note sounding at the freeze has its note-off deferred this long |
| Post-release burst | 324 edges in **5.50 ms** | the deferred backlog floods out at once |
| Min / median edge spacing | 30 µs / 60 µs | at the 31,250-baud bit floor (32 µs) — well-formed |
| Sub-30 µs edges | 0 real (27 are 1-sample quantization) | **no corruption, no dropped bytes** |

**Note-safety conclusion:** the blocking save does **not** corrupt or drop MIDI
— the backlog serializes out cleanly once XIP returns. But a note that is
sounding when the save lands has its **note-off deferred by the full ~37 ms**
(≈ 15% of a beat at 255 BPM stretched onto that note), after which the whole
queued backlog bursts out compressed into ~5.5 ms. The defect is **timing
(note-duration stretch + burst compression), not data integrity.**

### Conclusion for the 9.4 backend decision

Internal flash **cannot** meet the ≤1 ms transparent-MIDI bound, and no amount
of pause-protocol hardening changes that — the ~37 ms XIP-down window is
intrinsic to erasing/programming internal flash while executing from it. DMA
does not help the internal-flash case (a DMA channel cannot read flash while XIP
is down either, and the latency is the NOR die's erase time). The paths to a
transparent save are:

1. **A fully RAM-resident execution path** that keeps *both* cores sequencing
   through the write — no new BOM, but every hot-path instruction and ISR must
   be `__not_in_flash_func`, and the writing core still eats the flash erase
   latency.
2. **An external nonvolatile backend (FRAM over SPI)** — writes over SPI with
   XIP never disabled, no erase cycle (byte-writable), ~10¹²–10¹⁴ endurance, and
   the SPI write can be DMA-driven for a fully CPU-free save. This is the leading
   option task 9.3 prototypes. The backend-neutral `RecordStore` (task 10.1) was
   built so the A/B transactional commit (10.2) ports to it cleanly.

Task 9.3 (external FRAM evaluation) requires acquiring an FRAM part and is the
remaining input to the 9.4 checkpoint.

---

## Interpretation (logical jitter vs wire skew, Req 11.7)

- **Logical scheduling jitter is below the analyzer floor in every normal
  scenario** — single pattern at 40/120/255 BPM and 8 patterns at 120 BPM. The
  current scheduler hits its tick grid exactly; fixed per-tick cost is ~5.5 µs
  and per-byte dispatch cost is < 25 ns.
- **All observable MIDI timing variance is wire skew** — 320 µs/byte of
  unavoidable 31,250-baud serialization, which stacks to ~2.2 ms when many notes
  land on the same tick.
- **The one real timing defect is the blocking save**, which stalls MIDI for
  ~33 ms. Everything else is already within budget.

## BS05U measurement constraints (for repeatability)

- 12,288-sample buffer → span and resolution trade off; one capture cannot do
  both. Measure period and sub-µs pulse width in separate captures.
- Hardware trigger-timeout register maxes at 65,535 × 6.4 µs ≈ **419 ms**. A
  triggered capture whose `(window + timeout)` wait exceeds that is refused
  (`Required trigger timeout too long`) — keep windows short when triggering on
  a rare edge.
- Pre-trigger capture is unsupported above 5 MS/s (trigger_position forced to 0).
- Valid pure-logic modes: `LogicShot` (40–13 MS/s), `LogicFast` (10 MS/s),
  `Logic` (8 MS/s – 2.4 kS/s). Choose the sample period as an integer number of
  25 ns primary ticks inside one mode's range.
- Very short pulses (the < 25 ns dispatch pulse) are not reliably latched by the
  hardware trigger comparator; free-run and measure, or treat as below-floor.

## Reproducing

See the companion skill `genseq-bitscope-measurement` for the full step-by-step
procedure. In short:

```bash
# with scopething on PYTHONPATH and the BS05U on /dev/ttyUSB0:
python3 capture_baseline.py grid        # tick period + jitter
python3 capture_baseline.py tickwidth   # tick service duration
python3 capture_baseline.py midi        # dispatch vs wire
python3 capture_baseline.py save        # Ctrl+S pause (arm, then press Ctrl+S)
```
