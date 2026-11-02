# BUG B part 2 — handoff summary (cold start for a fresh session)

**One-liner:** pico-genseq M3 core1 takes an **asynchronous HardFault at ~14.9k
idle loops, but ONLY when the core0 UI task is running**. Part 1 of BUG B (a
`__cxa_guard` static-init hang) is already fixed and committed; this doc is the
remaining part 2.

Worktree: `/home/chris/Projects/pico/pico-genseq-wt-bisect-the-pico-genseq-core1-boot-fault`
Branch: `bisect/core1-boot-fault` (off `master`/`cae2666`; NOT pushed).
Full history + the broader bisection live in `DEBUG-core1-boot.md` next to this file
— read its top "SESSION FINDINGS 2026-11-02 (LATEST)" block first.

---

## The symptom (part 2)

- Build the full firmware (HOST_CONTROL + the UI task) with part-1 fix in place →
  flash → self-reset → **LED dark, serial silent** (core1 dead, core0 then wedged
  in `multicore_fifo_push_blocking`).
- With the UI task disabled (`-DGENSEQ_CORE1_BOOT_TRACE_NO_UI=ON`) core1 runs
  **2M+ loops forever, no fault**. So part 2 *requires the core0 UI task*.
- Deterministic: core1 HardFaults at **~14,900–15,020** task-loop iterations
  (`g_core1LoopCount ≈ 0x3a0d–0x3a8c`) every boot.

## What core1 is doing at the fault

- `g_core1Stage` **jitters between 2 and 5** run-to-run → the fault is
  **asynchronous** to core1's code position, not one faulting line.
- `g_core1LastCmd = 0` (NOOP), `g_core1ApplyCount = 0`, `g_core1LastRealCmd = 0xFF`
  → core1 is still **idle-spinning** and has **never applied a real command** when
  it dies. It faults the instant core0 starts the UI / restore FIFO burst.
- It is a genuine **HardFault**: ICSR `VECTACTIVE = 3`. The exception frame is
  **unstackable** (`g_core1FaultPC = 0`, all-zero frame at MSP) → a bus-error-type
  escalated HardFault. SP at fault is **sane** (~`0x20040f70`, well inside core1's
  `0x20040800–0x20041000` stack). So NOT a stack overflow.
- core0 at fault is blocked in `multicore_fifo_push_blocking` (pushing restore
  commands a dead core1 won't drain) — a **consequence**, not the cause.

## What was TRIED and the RESULT (ruled out)

| Hypothesis | Test | Result |
|---|---|---|
| XIP / flash-fetch disruption while core0 busy | Built `-DPICO_COPY_TO_RAM=1` (whole image in RAM) | **Still faults** at ~15k. NOT XIP. |
| Unhandled/!enabled IRQ taken on core1 | Read `NVIC_ISER` on hung core1 | `ISER = 0` — core1 enables **no** IRQs. ISPR bits (3,15,16) are core0's DMA/timer/FIFO pending, **not taken** on core1. Ruled out. |
| Fault in the command-apply (heap/vector) path | `g_core1ApplyCount` breadcrumb | `= 0` — core1 never reaches apply. Ruled out. |
| Corrupted `playing` / `output`/`clock` vtables (bad virtual call) | breadcrumb snapshot in `update()` | `playing=0`, both vtables valid flash addrs. Ruled out. |
| core0 stack overflow into core1 (old BUG A) | BUG A already fixed; core0 SP healthy | core1 still dies. Ruled out. |
| `__cxa_guard` static-init hang (this was **part 1**) | hoist `commandSnapshot` to file scope | **Fixed part 1** (core1 → 2M+ loops in NO_UI), but part 2 remains. |

## What we LEARNED (the useful shape of the bug)

1. Part 2 is **asynchronous** and **time/event-triggered**, not tied to a core1
   instruction or to processing any specific command — core1 dies mid-idle-spin.
2. It is **caused by core0 activity**: it appears exactly when `createUITask`
   runs and disappears entirely without it.
3. It is **not** XIP, **not** a core1 IRQ, **not** the heap/apply path — it's a
   bus-error HardFault from core1 reading/executing against something that goes
   bad when the UI subsystem starts.
4. Prime suspect is inside `createUITask`: `src/ui/hardware/driver/ws2812_dma.c`
   sets up **`DMA_IRQ_0` + handler `dma_complete_handler`**, which arms a repeating
   **`add_alarm_in_us(400, …)`** timer; plus the LED-matrix refresh, FRAM reads,
   and the `restoreSequencerFromState` FIFO burst all begin here.

## UPDATE 2026-11-02 — TRIGGER BISECTED to FRAM `load()` SPI reads (commit `4b0cddd`)

Added OFF-by-default isolation knobs and bisected the trigger with the self-reset
loop (core1 survives = `g_core1LoopCount` climbs, `g_core1FaultHit=0`):

| Build knob | What it removes | core1 result |
|---|---|---|
| `GENSEQ_CORE1_BOOT_TRACE_NO_DMA` | WS2812 DMA/PIO/IRQ + `add_alarm_in_us` | **still faults** ~14832 → DMA/alarm NOT it |
| `GENSEQ_CORE1_BOOT_TRACE_NO_RESTORE` | `restoreSequencerFromState` FIFO burst | **still faults** ~14860 — BUT core0 now boots to `H:pong` (the burst only decided whether core0 wedged in `multicore_fifo_push_blocking`) |
| `GENSEQ_CORE1_BOOT_TRACE_NO_FRAM` | `framSpi->init()` + `recordStore->load()` | **ALIVE**, 1M+ loops → FRAM is the trigger |
| `GENSEQ_CORE1_BOOT_TRACE_NO_FRAM_LOAD` | only the `load()` read transfers (keeps `init()`) | **ALIVE**, 1M+ loops → it's the **SPI READ TRAFFIC**, not the `spi_init` bring-up |

**Conclusion:** core0's FRAM **`recordStore->load()` blocking SPI1 read transfers**
(GP10-13, `spi_read_blocking`/`spi_write_read_blocking`) **asynchronously fault
core1** ~15k idle-loops in. The FRAM driver (`Mb85rsFramSpi.h`) has **no DMA, no
IRQ** — just `spi_init` + blocking transfers. core1's idle `update()` path touches
no timer/SPI/spinlock (pure arithmetic: `g_soundingNotes = soundingCount()` then
`if(!playing) return`), so the fault is **externally imposed on core1**, not in
core1's own code. Faulting PC is **unrecoverable** (genuine stacking fault: frame
at FaultSP `0x20040f70` is all-zero; my core1 HardFault handler fires but the HW
never stacked a frame). Recurring non-code words on core1's stack top:
`0x03021e00`, `0x28070302`, `0x64000000` (look like packed FRAM/command data).

## UPDATE 2 (2026-11-02, later) — mechanism hunt; KEY control: M2 does the same FRAM load and survives

Dug into *why* core0's FRAM `load()` SPI reads fault core1. Results:

- **Every software mechanism ELIMINATED:** `spi_read_blocking` is a pure RAM
  busy-poll (no IRQ/spinlock/DMA); the FRAM read buffer `cfg` is at `0x200016f4`
  (far from core1's stack `0x20040800–0x20041000`); core1's stack bank (SCRATCH_X)
  holds only `core1_stack` (no aliasing); core1 calls no `printf`; the LCD/I2C
  `Display` is NOT instantiated by `UIController` (no GP10-13 I2C/SPI pin clash);
  `printf` + TinyUSB are present in the NO_FRAM build where core1 LIVES.
- **`readRecord` heap alloc was NOT the cause.** `RecordStore::readRecord` did a
  per-call ~4 KB `std::vector` on core0 (and the `writeSlotRegion` comment flags
  the SAME bug already fixed on the SAVE path). Replaced it with a static decode
  buffer (committed `6b23f07`, a correct improvement) — **core1 still faults**, so
  core0 load-path heap contention is not it.
- **SLOW_SPI test (`GENSEQ_CORE1_BOOT_TRACE_SLOW_SPI`, 100 kHz vs 8 MHz):** core1
  fault moved from loopCount **14,858 → 62,929 (~4.2×)**. So the fault fires at a
  fixed point **DURING the SPI read transfer** and **scales with transfer
  duration** — not a fixed loop count or one-shot event.
- **★ KEY CONTROL — M2 (`ff900c9`) does the IDENTICAL FRAM boot load** (same
  `framSpi->init()` → `recordStore->load()` → `restoreSequencerFromState`, same
  `readRecord` `std::vector`) and **core1 survives (boots H:pong).** Therefore the
  FRAM load is only the **trigger/timing**; the ROOT CAUSE is an **M3 change to
  core1's own path** that made core1 fragile to FRAM-read-time activity. core1 runs
  ~15k idle loops fine, then dies *when the FRAM read runs*.
- Fault is a HardFault with an **unstackable frame** (SP `0x20040f70` is valid and
  shallow — NOT stack overflow). PICO_COPY_TO_RAM (whole image in RAM) still
  faults → not XIP starvation.

### Where to dig next (fresh session) — REVISED by UPDATE 2
1. **Diff M2→M3 on core1's EXACT idle path** and run it to ground:
   `sequencer_task` (identical M2↔M3), `Sequencer::update()` idle branch,
   `receiveCommandInto` + `PatternSnapshot::clear()` (M3 enlarged the snapshot with
   `GenerationParameters`), `g_commandSnapshot` (hoisted to file scope in part-1
   fix). Something M3 added to what core1 touches every idle loop is the fragility.
2. Since the fault scales with SPI *duration*, test whether it's **sustained SPI1
   bus activity** specifically: add a knob doing a SHORT (8-byte) vs the full
   ~4088-byte FRAM read — does a short read still fault? If only long reads do,
   it is duration/bus/electrical; if even a short read faults, it is the act of
   any SPI1 transfer.
3. Consider an **electrical / power** interaction (SPI1 switching on GP10-13 vs the
   probe-powered rail) — but PROVE it, do not assume; the M2 control argues against
   pure electrical since M2 drives the same SPI and is fine.
4. The faulting PC is unrecoverable (stacking fault) — reason from the trigger.
5. Pragmatic FIX regardless of mechanism: M2 shipped FRAM saves working, so
   compare M2's vs M3's core1 steady-state memory/stack use; if M3 core1 is simply
   closer to an edge, the fix may be shrinking core1's per-loop footprint.

### Original "where to dig next" (pre-UPDATE-2, partly superseded)
1. **Why do SPI1 blocking reads on core0 fault core1?** The driver is clean, so
   suspect a lower layer: a shared **hardware spinlock** (SDK `spi_*` / `sleep_*` /
   the default alarm pool take `PICO_SPINLOCK_ID_*`), a **clock/PLL/RESETS**
   perturbation, or a **bus-fabric / SIO** interaction. Note the fault needs the
   bare core0 UI `sleep_ms(1)` loop running *too* (NO_UI alone = core1 fine), so it
   may be FRAM-reads **plus** `sleep_ms` timer/alarm activity together.
2. Try: replace `recordStore->load()`'s `spi_read_blocking` with a hand-rolled
   polled SPI read (no SDK blocking helper) — does it still fault? Isolates the SDK
   helper vs raw SPI.
3. Try: a single FRAM read vs the full load loop — does one transfer fault core1,
   or only sustained traffic? (Add a knob that does N reads.)
4. The faulting PC is lost to the stacking fault — stop chasing it; reason from the
   trigger instead.

## Recommended next steps (ORIGINAL — superseded by the UPDATE above for step 1)

1. **Bisect within `createUITask`**: disable just the WS2812 DMA + alarm setup
   (keep the rest of the UI) and self-reset-test — does core1 survive? That
   splits "DMA/alarm" from "FIFO restore burst" / "FRAM".
2. **Breadcrumb core0**: record core0 milestones (FRAM done, restore-burst start,
   first `sendCommand`, DMA init, first alarm) so you can read, after the fault,
   exactly which core0 action coincides with core1's loop-count freeze.
3. Suspect **cross-core memory corruption** of the `Sequencer` / `noteLifecycle` /
   `metrics` object that core1 reads every `update()` — look for anything core0
   writes that aliases core1-owned memory once the UI starts (shared buffers,
   an alarm/DMA writing near `0x20040xxx`, a pointer handed across the FIFO).
4. Get the faulting PC despite the unstackable frame: the naked core1 HardFault
   handler is already installed (`GENSEQ_CORE1_BOOT_TRACE`); try also reading the
   faulting **address** via a data-watchpoint on the suspected region once a
   stable boot allows arming it (single-core wp, not dual-core).

## Tooling (all proven this session)

- **Self-reset loop is reliable; dual-core SWD breakpoints/watchpoints are NOT**
  (multidrop flaky). Prefer the firmware breadcrumb method.
- Env + helpers recreated each session under `$KIROCREW_SCRATCH`:
  `genseq-env.sh` (PICO_SDK_PATH=/home/chris/Projects/pico/pico-sdk; CMake uses
  the installed `~/.pico-sdk/sdk/2.1.1`; toolchain `~/.pico-sdk/toolchain/14_2_Rel1`),
  `flash.sh <elf>`, `wdreset.sh` (hands-off watchdog reboot — see DEBUG-core1-boot.md),
  `probe.py` (H:ping→H:pong over /dev/ttyACM0).
- Build flags: `-DGENSEQ_HOST_CONTROL=ON -DGENSEQ_CORE1_BOOT_TRACE=ON`
  (+ `-DGENSEQ_CORE1_BOOT_TRACE_NO_UI=ON` for the isolation build,
  `-DGENSEQ_PERFORMANCE_TRACE_ENABLED=ON -DGENSEQ_TRACE_FAULT_PIN=6`).
- Breadcrumb symbols (addresses MOVE per build — resolve with
  `arm-none-eabi-nm genseq.elf | grep g_core1`): `g_core1Stage`,
  `g_core1LoopCount`, `g_core1LastCmd`, `g_core1LastRealCmd`, `g_core1ApplyBranch`
  (1=GATE 2=PITCH 3=VEL 4=GENPARAMS 5=PLAY 6=STOP 7=BPM 8=ACT 9=DEACT 10=ADD
  11=REMOVE 100=survived-apply), `g_core1ApplyCount`, `g_core1ApplyParam`,
  `g_core1Playing`, `g_core1OutputVtbl`, `g_core1ClockVtbl`,
  `g_core1Fault{Hit,PC,LR,R0,SP}`.
- Bench must be left on **M2 (`ff900c9`)** known-good when pausing (boots to
  `H:pong`); `reset run` is forbidden on this multidrop target — use `wdreset.sh`.

## Commits on `bisect/core1-boot-fault` (not pushed)

- `f4c9054` fix(m3): hoist core1 commandSnapshot to file scope (BUG B part 1)
- `ab1ac21`, `1b6f58f` diag harness (OFF by default), `ed5f770`, `412b1a1` docs.
- Host suite: **41/41** green with the diagnostics inert.
