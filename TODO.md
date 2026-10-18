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

- [ ] Mount the FRAM breakout on **SPI1** (keep it off the QSPI/XIP bus — the
      whole point is XIP stays up during writes). SPI0 default pins collide with
      the LED matrix on GP16.
- [ ] Proposed pins (CONFIRM against the LCD I2C SDA/SCL pins in
      `src/ui/hardware/Display.cpp` before soldering/wiring — those are passed at
      runtime and must not clash):

      | FRAM pin | Pico GPIO | SPI1 function |
      |---|---|---|
      | SCK  | GP10 | SPI1 SCK  |
      | MOSI | GP11 | SPI1 TX   |
      | MISO | GP12 | SPI1 RX   |
      | CS   | GP13 | SW-driven CS (GPIO) |
      | VCC  | 3V3  | — |
      | GND  | GND  | — |
      | WP/HOLD | 3V3 (tie inactive) | — |

- [ ] Confirmed-used pins to avoid: GP8/9 (MIDI UART1), GP16 (LED matrix),
      GP25 (onboard LED), GP2–6 (trace build), and the LCD I2C pins (TBD).

### 2. Firmware driver

- [ ] Add FRAM pin defines to `src/config/pins.h`
      (`FRAM_SPI`, `FRAM_SPI_PIN_SCK/MOSI/MISO/CS`).
- [ ] Implement an `Mb85rsFramStore` behind the existing **`IFlashStore`**
      interface (`src/persistence/`), so the backend-neutral **`RecordStore`**
      (task 10.1) sits on top unchanged. Bring-up: RDID device-ID check, then a
      write/read-back round-trip at a known address.
- [ ] Keep the write path XIP-independent and interruptible: dribble the record
      out in bounded SPI chunks via the **`SaveService`** state machine
      (task 8.2) — no core-pause, no XIP disable. Optionally DMA the SPI TX
      (SPI1 TX DREQ) for a CPU-free transfer (note for 10.3, not required for the
      9.3 measurement).

### 3. Measurement (BitScope, same rig as 9.2)

- [ ] Rebuild `build-trace` on `agents` with the FRAM-backed save; keep the
      trace pins (Save=GP5, Tick=GP2, Wire=GP8). Add a trace pin for FRAM-CS
      activity if a spare GPIO allows, to time the SPI transfer directly.
- [ ] Capture Ctrl+S under the **same worst-case live load as 9.2** (10 patterns,
      120 & 255 BPM) and assert the transparency claim:
      - [ ] **Ticks continue** through the save (no gap on L0).
      - [ ] **MIDI continues** through the save (no silence on L5 ≥ 1 ms).
      - [ ] Measure the FRAM write duration itself (expect tens–hundreds of µs
            vs internal flash's ~37 ms).
- [ ] Save captures to `docs/development/baseline/captures/` and write up the
      9.3 result in `docs/development/baseline/README.md`.

### 4. Documentation for the 9.3 deliverable (per task 9.3 acceptance)

- [ ] Document for FRAM: **capacity** (256 KB), **latency** (measured write/read),
      **endurance** (~10¹³), **bus/pin use** (SPI1 + CS), **cost**, and
      **integration complexity** (new driver + A/B commit port) — against the
      internal-flash and RAM-resident-flash alternatives.

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
