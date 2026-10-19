# TODO

## Milestone 2 — Task 9.3: External FRAM evaluation (hardware adaptation)

Ordered part: **Adafruit SPI FRAM breakout — MB85RS2MTA**
(2 Mbit / **256 KByte**, SPI, 40 MHz max, ~10¹³ write endurance, 3.3 V).
This is the external-nonvolatile-backend prototype that, with the task-9.2
captures, feeds the **9.4 backend-selection checkpoint** (needs explicit
approval before committing a backend).

Context from 9.2 (`docs/development/baseline/README.md`): the internal-flash
Ctrl+S save freezes **both cores for ~37 ms** (zero ticks, zero MIDI) because
writing internal flash disables XIP. The FRAM hypothesis: writing an external
SPI device never touches XIP, so the save becomes a background SPI transfer the
sequencer never notices → transparent save within the ≤1 ms MIDI budget.

### Part-specific gotchas to honor in the driver

- [ ] **3-byte (24-bit) address.** The MB85RS2M (256 KB) uses a **3-byte**
      address in READ/WRITE commands — unlike the smaller MB85RS64/256 parts
      (2-byte). Hard-coding a 2-byte address silently wraps at 64 KB. Make the
      address width explicit.
- [ ] **WREN before every write.** Each WRITE (0x02) must be preceded by a
      WREN (0x06) opcode; the write-enable latch auto-clears after each write.
- [ ] **Opcodes:** WREN 0x06, WRDI 0x04, RDSR 0x05, WRSR 0x01, READ 0x03,
      WRITE 0x02, RDID 0x9F (verify device ID on bringup).
- [ ] **SPI mode 0 or 3**, MSB-first. Start conservative at ~8–10 MHz, then
      raise toward 40 MHz once reads/writes verify.

### 1. Wiring (bench)

Mount the FRAM breakout on **SPI1** (keep it off the QSPI/XIP bus — the whole
point is XIP stays up during writes). SPI0 default pins collide with the LED
matrix on GP16, so SPI1 on GP10–13 is the clean, conflict-free choice.

**Pin-clash check (VERIFIED 2026-10-18):** the only GPIOs actually claimed in the
live build are GP8/GP9 (MIDI UART1), GP16 (LED matrix) and GP25 (onboard LED).
The LCD `Display` class takes its I2C SDA/SCL pins as constructor arguments and
is **not instantiated** in the live build (`src/ui/UIController.cpp` notes the
LCD is not wired and `pins.h` defines no I2C pins), so there is **no LCD pin to
clash with today**. GP10–13 are free. Re-check if the LCD is ever wired in.

These default pins are baked into `src/persistence/Mb85rsFramSpi.h`
(`Pins{ instance=spi1, sck=10, mosi=11, miso=12, cs=13, baud=8 MHz }`).

| FRAM breakout pin | Pico GPIO | Physical pin | Function / notes |
|---|---|---|---|
| SCK      | GP10 | 14 | SPI1 SCK (clock) |
| MOSI     | GP11 | 15 | SPI1 TX → FRAM SI |
| MISO     | GP12 | 16 | FRAM SO → SPI1 RX |
| CS       | GP13 | 17 | Software-driven CS (GPIO, active-low, idles high) |
| VIN/VCC  | 3V3  | 36 | 3V3(OUT); breakout has onboard regulator + level shift |
| GND      | GND  | 23 | Common ground (nearest to the signal pins) |
| WP       | 3V3  | —  | Write-protect, active-low. Breakout pulls high → may leave floating; tie to 3V3 if writes are flaky |
| HOLD     | 3V3  | —  | Hold, active-low. Driver never uses it; same as WP |

> **Pin-number gotcha:** "GP10" is the GPIO number the firmware uses; it is
> **physical pin 14** on the board. Count physical pins 1–40 down the board edge;
> the silkscreen shows the `GPnn` numbers.

**Clock speed:** the driver starts conservative at **8 MHz** (`Pins::baud`). The
MB85RS2MTA supports up to 40 MHz — raise `baud` later for a faster save once
reads/writes verify. 8 MHz is plenty for a config record and more tolerant of
breadboard wiring.

```
   Adafruit MB85RS2MTA FRAM breakout            Raspberry Pi Pico (USB at top)
  ┌───────────────────────────────┐           ┌───────────────┬───┬───────────────┐
  │                               │           │ GP0        1 ●─┤USB├─● 40  VBUS     │
  │   VIN  GND  WP  SCK  MISO     │           │ GP1        2 ●─┤   ├─● 39  VSYS     │
  │   HOLD CS   MOSI              │           │ GND        3 ●─┤   ├─● 38  GND      │
  │    │   │    │    │    │       │           │ GP2        4 ●─┤   ├─● 37  3V3_EN   │
  └────┼───┼────┼────┼────┼───────┘           │ GP3        5 ●─┤   ├─● 36  3V3(OUT) │◀── VIN, WP, HOLD
       │   │    │    │    │                   │ GP4        6 ●─┤   ├─● 35  ADC_VREF │
       │   │    │    │    └── MISO ───────────┼─● GP12   16 ───┤   ├─● 34  GP28     │
       │   │    │    └─────── SCK  ───────────┼─● GP10   14 ───┤   ├─● 33  GND      │
       │   │    └──────────── MOSI ───────────┼─● GP11   15 ───┤   ├─● 32  GP27     │
       │   └───────────────── CS   ───────────┼─● GP13   17 ───┤   ├─● 31  GP26     │
       │                                       │ GP8  (MIDI TX) ┤   ├  ...          │  ◀ GP8/9 = MIDI, leave
       └── VIN/WP/HOLD → 3V3(OUT) pin 36        │ GP9  (MIDI RX) ┤   ├               │
           GND         → GND     pin 23         │ GND       23 ●─┤   ├               │◀── GND
                                                └───────────────┴───┴───────────────┘

  Signal pins used:  GP10=SCK(14)  GP11=MOSI(15)  GP12=MISO(16)  GP13=CS(17)
  Power:             VIN/WP/HOLD → 3V3(OUT) pin 36,  GND → pin 23
  (Breakout silkscreen pin order varies by revision — match by LABEL, not position.)
```

**Bring-up order (before any measurement):** power on → `Mb85rsFramSpi::init()`
→ **RDID (0x9F)**. A healthy MB85RS2M returns manufacturer `0x04` (Fujitsu) + the
2 Mbit density/product code. `0x00` or `0xFF` ⇒ wiring fault — almost always
MISO/MOSI swapped or CS not reaching GP13. This is exactly what
`example_fram_driver` exercises, so it is the first on-target check.

Confirmed-used pins to avoid: GP8/9 (MIDI UART1), GP16 (LED matrix),
GP25 (onboard LED), GP2–6 (trace build only).

### 2. Firmware driver

- [ ] Add FRAM pin defines to `src/config/pins.h`
      (`FRAM_SPI`, `FRAM_SPI_PIN_SCK/MOSI/MISO/CS`). (Deferred to 10.3 live
      integration; harness uses `Mb85rsFramSpi` default Pins{} = SPI1 GP10-13.)
- [x] Implement an `Mb85rsFramStore` behind the existing **`IFlashStore`**
      interface (`src/persistence/`), so the backend-neutral **`RecordStore`**
      (task 10.1) sits on top unchanged. Bring-up: RDID device-ID check, then a
      write/read-back round-trip at a known address.
      DONE: `Mb85rsFram` (SDK-free) + `Mb85rsFramSpi` (firmware) committed 35843c0;
      RDID + 3-byte addressing verified on real silicon (fram_bringup, d3cb4d9):
      RDID=0x04 7F 48 03 (Fujitsu), write@0x012345 round-trip PASS.
- [x] Keep the write path XIP-independent and interruptible: dribble the record
      out in bounded SPI chunks via the **`SaveService`** state machine
      (task 8.2) — no core-pause, no XIP disable. Optionally DMA the SPI TX
      (SPI1 TX DREQ) for a CPU-free transfer (note for 10.3, not required for the
      9.3 measurement).
      VERIFIED transparency directly (no core-pause, no XIP disable) via the
      Route A trace harness; `SaveService`-chunked dribble is the 10.3 live wiring.

### 3. Measurement (BitScope, same rig as 9.2)

- [x] ~~Rebuild `build-trace` with the FRAM-backed save~~ — done differently:
      Route A standalone trace harness `src/tools/fram_save_trace.cpp`
      (`GENSEQ_BUILD_FRAM_SAVE_TRACE`) drives a REAL `Mb85rsFram::eraseAndProgram`
      of the real serialized record while a core emits the 255 BPM Tick+MIDI
      waveform. Trace pins Tick=GP2/L0, MIDI=GP3, Save=GP5/L3. Self-triggered
      saves once/sec (no keyboard needed).
- [x] Capture under worst-case load and assert the transparency claim:
      - [x] **Ticks continue** through the save: 27 tick edges inside a 32.4 ms
            FRAM write, intervals steady at 2.42–2.46 ms (= 2.451 ms period),
            longest gap 0.572 ms. (vs 9.2 flash: ZERO ticks for the interior.)
      - [x] **MIDI continues** through the save: 157 MIDI edges inside the window.
      - [x] FRAM write duration measured: **real 676 B save = ~1.81 ms**
            (vs internal flash ~37 ms freeze — ~20×). Stress 12 KB write = 32.4 ms.
- [x] Save captures to `docs/development/baseline/captures/` and write up the
      9.3 result in `docs/development/baseline/README.md`.
      DONE: `captures/fram_transparency_255bpm_stress.npy` +
      `capture_fram_transparency.py` + full README §"Task 9.3".

### 4. Documentation for the 9.3 deliverable (per task 9.3 acceptance)

- [x] Document for FRAM: **capacity** (256 KB), **latency** (measured write/read),
      **endurance** (~10¹³), **bus/pin use** (SPI1 + CS), **cost**, and
      **integration complexity** (new driver + A/B commit port) — against the
      internal-flash and RAM-resident-flash alternatives.
      DONE in README §"Task 9.3" (latency ~1.81 ms measured; 40 MHz-rated part;
      remaining: 9.4 side-by-side presentation + explicit backend approval).

### 5. Decision & integration (gated)

- [ ] **9.4 checkpoint:** present 9.2 + 9.3 evidence side-by-side; obtain
      **explicit approval** of the v1 backend before any backend-specific commit.
- [ ] On approval: wire **10.2** (A/B transactional commit) + **10.3**
      (integrate + validate) onto the selected backend. The A/B logic already
      exists backend-neutral in `RecordStore` (10.1).
- [ ] Then close the hardware-validation gates: **12.3** (RP2040 timing/MIDI),
      **12.4** (note-safety + endurance), **12.5** (retain evidence + mark
      Milestone 2 complete).

---

## Deferred debt (not blocking)

- [x] **6.4 firmware cutover:** DONE. The core1 receive loop no longer decodes
      into `std::vector` via `receiveCommand()`. It now calls
      `commands::receiveCommandInto(snapshot)`, which decodes each multiword
      pattern-set payload into a reused, function-static fixed-capacity
      `sequencer::PatternSnapshot` (allocated once, never on the per-command
      path — Req 7.9), and `Sequencer::processCommand(msg, snapshot)` applies the
      three set commands from that snapshot. The public `CommandMessage` API and
      the legacy scalar `receiveCommand()`/`processCommand(msg)` are left intact
      (host tests + UI producers unchanged), so the big `CommandMessage`-API
      ripple was avoided — the cutover added an allocation-free sibling path
      rather than rewriting the transport. `example_command_snapshot_path.cpp`
      proves the snapshot path emits the identical MIDI stream as the scalar
      path; host suite 31/31 green and clean under ASan+UBSan; firmware links.
      (Full `CommandInbox` SPSC-ring transport remains a separate 6.1/6.2
      integration, distinct from this 6.4 allocation fix.)
