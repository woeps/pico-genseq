# Debug brief: core1 boot fault (M3) — bisection handoff

Focused hardware bisection for a reproducible RP2040 **core1 boot fault** in
pico-genseq Milestone 3. Everything needed to start cold is here.

Repo: `/home/chris/Projects/pico/pico-genseq`, branch `agents`.

> ## SESSION FINDINGS 2026-11-02 (LATEST) — BUG B is TWO faults; part 1 FIXED, part 2 localized — READ THIS FIRST
>
> Continued with the breadcrumb harness. BUG B resolved into **two distinct core1
> faults**; the first is fixed and committed.
>
> ### BUG B part 1 — core1 hangs in bootrom memset during `__cxa_guard` static init — **FIXED** (`f4c9054`)
> The decode buffer `commandSnapshot` was a **function-local static** in
> `sequencer_task()`. Its lazy, `__cxa_guard`-protected first-use construction
> runs **on core1**, and on silicon that guarded construction **hung core1 inside
> the bootrom `memset` store loop** (ROM `0x2710`, func `'MS'`@`0x26dd`; the
> dispatch table at `0x20001420` and the size arg were both fine — the store loop
> simply never progressed when core1 ran alone; it only advanced one bus
> transaction per SWD resume). Fix: hoist to a **file-scope POD static**
> `g_commandSnapshot` (zero-init in BSS at image load, no guard, no core1 runtime
> ctor). **Proven on hardware**: with the UI disabled, core1 went from frozen
> (0/2576 loops) to **2M+ loops and climbing**, no fault. Host 41/41.
> ROM funcs were mapped from the live ROM table: `0x2641`='MC' memcpy,
> `0x2629`='C4' memcpy44, `0x26dd`='MS' memset.
>
> ### BUG B part 2 — async core1 HardFault at ~15k loops, ONLY with the UI task active — **STILL OPEN**
> With part 1 fixed, the full build (UI task on) still fails: core1 runs
> **~14,900–15,000 loops** then **HardFaults** (`g_core1FaultHit` set; ICSR
> `VECTACTIVE=3` confirms HardFault). Hard facts:
> - `applyCount=0`, `lastRealCmd=0xFF` → core1 **never completes a real command
>   apply**; it faults during the FIRST real FIFO receive, as core0 starts the
>   restore burst. `g_core1Stage` at fault **jitters (2 and 5)** run-to-run →
>   the fault is **asynchronous** to core1's code position, not a specific line.
> - core0 is blocked in `multicore_fifo_push_blocking` (pushing restore-burst
>   commands a dead core1 won't drain) — a consequence, not the cause.
> - **RULED OUT for part 2:** XIP/flash-fetch disruption (built with
>   `PICO_COPY_TO_RAM=1`, whole image in RAM — core1 STILL faults at ~15k, same
>   as flash); unhandled IRQ on core1 (`NVIC_ISER=0` — core1 enables no IRQs;
>   bits in ISPR are just core0's DMA/timer/FIFO pending, not taken on core1);
>   command-apply heap/vector path (never reached, applyCount=0); corrupted
>   `playing`/vtables (part-1 pass showed them sane); core0 stack overflow (BUG A,
>   fixed). The exception frame is **unstackable** (`g_core1FaultPC=0`), so it is a
>   bus-error-type HardFault, escalated, SP sane (~`0x20040f70`).
> - **Trigger correlation:** part 2 appears exactly when `createUITask` runs
>   (`ws2812_dma.c` sets up `DMA_IRQ_0` + its handler `dma_complete_handler`, which
>   arms a repeating `add_alarm_in_us` timer; LED matrix refresh + FRAM + the
>   restore FIFO burst all begin here). NO_UI build has none of this and (post
>   part-1 fix) core1 runs forever.
> - **Leading hypotheses for a fresh session:** (a) a cross-core **memory
>   corruption** of the `Sequencer`/`noteLifecycle`/`metrics` object core1 reads in
>   `update()` the instant core0's UI/DMA/alarm starts (async bus fault on a
>   bad-address read); (b) the **alarm pool / `add_alarm_in_us`** or DMA setup on
>   core0 disturbing shared state core1 touches; (c) the SIO FIFO interaction at
>   first push. Next steps: breadcrumb core0's `createUITask`/`restoreSequencerFromState`
>   milestones to pin what core0 did at the exact loop count; add a watchpoint on
>   the `Sequencer` object header once a stable boot allows it; try disabling just
>   the WS2812 DMA/alarm (keep the rest of the UI) to bisect within `createUITask`.
> - Breadcrumbs for part 2 committed (`1b6f58f`, OFF by default):
>   `g_core1LastRealCmd`, `g_core1ApplyBranch` (1=GATE 2=PITCH 3=VEL 4=GENPARAMS
>   5=PLAY 6=STOP 7=BPM 8=ACT 9=DEACT 10=ADD 11=REMOVE 100=survived),
>   `g_core1ApplyCount`, `g_core1ApplyParam`. Resolve addrs per build with
>   `arm-none-eabi-nm | grep g_core1`.
>
> ## SESSION FINDINGS 2026-11-02 (LATE) — GPIO/RAM-breadcrumb pass — READ THIS FIRST, it OVERTURNS the launch-fault story below
>
> Using a firmware breadcrumb harness (committed, OFF by default — build with
> `-DGENSEQ_CORE1_BOOT_TRACE=ON`; read the `g_core1*` BSS symbols over SWD after a
> self-reset) the core1 fault was traced far more precisely than the SWD-only
> passes managed. **Everything in the older blocks claiming "core1 faults at
> launch / before `core1_trampoline` / before its own code" is WRONG** — that was
> stale double-fault-lockup state from a prior build read as the live fault.
>
> ### What core1 actually does (current `bisect/core1-boot-fault` tip)
> - core1 **launches fine, runs its real task loop** (`sequencer_task` →
>   `receiveCommandInto` → `processCommand` → `update`) **thousands of times**,
>   then dies. It is NOT an early/launch fault.
> - **Deterministic**: full build (HOST_CONTROL + TRACE + UI) → core1 **HardFaults**
>   at ~**15,380** loops (`g_core1LoopCount≈0x3c17`), `lastCmd=NOOP` (idle spin),
>   `playing=0` (NOT corrupted), `output`/`clock` vtable ptrs VALID flash addrs
>   (so NOT a corrupted-vtable virtual call in core1's own object). The core1-local
>   naked HardFault handler fires (`g_core1FaultHit=0xFA017ED0`) but the stacked
>   exception frame is **all-zero / unstackable** → the stacked PC is unrecoverable
>   (stacking itself faulted; EXC_RETURN=0xfffffff9 = MSP, no FP).
> - **Isolation test** (`-DGENSEQ_CORE1_BOOT_TRACE_NO_UI=ON`, which skips
>   `createUITask` so core0 just `tight_loop_contents()`): core1 **STILL dies**,
>   deterministically at **2576** loops (`0xa10`), but DIFFERENTLY — no HardFault
>   (`faultHit=0`); core1 is **HUNG in the BOOTROM** (`PC≈0x2734`, static across
>   samples; loop count frozen) with `LR` → `receiveCommandInto`'s `__wrap_memset`
>   path. Same via a clean debugger boot (`reset halt`+`resume`), so NOT a
>   watchdog-reset artifact.
> - **Therefore core0's UI/application code is NOT the corruptor** (core1 dies with
>   the UI task entirely disabled). UI activity only shifts the failure TIMING
>   (15k vs 2.5k loops) and MODE (HardFault vs bootrom hang).
> - **RULED OUT this pass:** XIP-during-flash-write (no `flash_range_program`/
>   `erase` is even linked into the binary — the FRAM store does no flash writes);
>   unprotected cross-core heap race (malloc/free/realloc all serialize on the one
>   `__wrap_free` mutex `0x20002584`); `GateSet`/`PitchSet` rule-of-three (they hold
>   `std::vector` by value, implicit deep copy is correct); core0 stack overflow
>   into core1 (BUG A, fixed; core0 SP now healthy and core1 still dies).
>
> ### Leading hypothesis to chase next (fresh session, self-reset loop is cheap)
> core1 **hangs/faults inside a BOOTROM function** (looks like the ROM
> `memset`/`memcpy` reached from `receiveCommandInto`/`snapshot.clear()`). The
> RP2040 bootrom's shared routines are NOT guaranteed concurrent-safe across both
> cores. Next steps, most decisive first:
>   1. **Breadcrumb core0 too** (what is core0 doing — esp. any bootrom/ROM call,
>      `memcpy`/`memset`, FRAM/SPI, USB IRQ — at the instant core1's loop count
>      freezes). Confirm/deny concurrent bootrom use.
>   2. **Build M2 (`ff900c9`) with an equivalent loop-count breadcrumb** and check
>      whether M2's core1 loops forever (isolates the regression to M3 core1 code
>      vs a latent concurrency bug M3 merely exposes by timing).
>   3. Identify bootrom `0x2734` precisely (ROM func table) — is it `memset`,
>      `memcpy`, or a wait/holding routine.
>   4. Try replacing the SDK bootrom-accelerated `memset`/`memcpy` on the core1
>      path with a plain byte loop (or `-DPICO_USE_OPTIMISTIC_SBRK`/ROM-memcpy off)
>      and see if core1 survives — would confirm the ROM-function concurrency bug.
>
> ### Breadcrumb harness (committed `ab1ac21`, all OFF by default; host 41/41 green)
> - `GENSEQ_CORE1_BOOT_TRACE`: `g_core1Stage` (1=entry 2=looptop 3=after-receive
>   4=before-processCommand 5=after 6=after-update), `g_core1LoopCount`,
>   `g_core1LastCmd`, `g_core1UpdateN`, `g_core1Playing`, `g_core1OutputVtbl`,
>   `g_core1ClockVtbl`, plus a naked core1 HardFault handler →
>   `g_core1Fault{Hit,PC,LR,R0,SP}`. Resolve the symbol addresses per build with
>   `arm-none-eabi-nm genseq.elf | grep g_core1` (they move when BSS changes).
> - `GENSEQ_CORE1_BOOT_TRACE_NO_UI`: skip `createUITask` (core0 idles).
> - Links `hardware_exception` (temporary). REMOVE the whole harness once fixed.
> - Scratch helpers this session: `$KIROCREW_SCRATCH/{genseq-env.sh,flash.sh,wdreset.sh,probe.py}`
>   (scratch is per-session; recreate from the brief/env if gone). Build+bench env:
>   `PICO_SDK_PATH=/home/chris/Projects/pico/pico-sdk` (CMake actually uses the
>   installed `~/.pico-sdk/sdk/2.1.1`), toolchain `~/.pico-sdk/toolchain/14_2_Rel1`.
>
> ## SESSION FINDINGS 2026-11-02 (read this FIRST — supersedes the ranked plan below)
>
> Worktree `/home/chris/Projects/pico/pico-genseq-wt-bisect-the-pico-genseq-core1-boot-fault`,
> branch `bisect/core1-boot-fault` (off `master`/`cae2666`; the old `agents`
> branch was merged into `master`). Fault reproduced cleanly and bisected on
> silicon. **TWO DISTINCT BUGS** were confirmed — do not conflate them.
>
> ### BUG A — core0 boot-path stack overflow — **FIXED & VERIFIED** (committed)
> `UIController::initialize()` held `PersistableConfig cfg` (~2108 B) **and**
> `UIState bootState` (~1436 B + a `setState()` copy) as by-value locals on
> core0's 2 KB stack, on the deepest boot path (FRAM load +
> `restoreSequencerFromState`). Measured `initialize()` frame = **4260 B**
> (bigger than the whole 2 KB stack). SWD proved core0 SP reached **`0x20040e68`**,
> ~2.4 KB BELOW its own `__StackBottom` (`0x20041800`) — i.e. core0 overflowed
> down into core1's adjacent launch-stack region.
> Fix (commits `a2a6fde` + `9f0c982`): `cfg` → function-local `static`; fold the
> restored config into StateManager's UIState **in place** via new
> `mutableState()`/`notifyStateChanged()`. `initialize()` frame 4260 B → ~60 B;
> core0 SP after fix = **`0x20041ee8`** (healthy). Host suite **41/41** green.
> Also kept: `reserve(PATTERN_CAPACITY)` in the Sequencer ctor (defense-in-depth).
>
> ### BUG B — core1 faults at launch — **STILL OPEN**, but much narrower now
> With BUG A fixed and core0's stack proven pristine, **core1 STILL faults** →
> LED dark, serial silent. So BUG B is INDEPENDENT of core0's stack (the
> "core0 overflow corrupts core1's launch stack" theory is **disproven** by the
> fix not helping). Hard facts captured:
> - core1 is in a **double-fault LOCKUP** (PC `0xfffffffe`, LR `0xfffffff9`).
> - core1 faults **before `core1_wrapper` (`0x10012010`) and before
>   `sequencer_task` (`0x10003594`)** — i.e. in the SDK launch/`core1_trampoline`
>   /`runtime_run_per_core_initializers` path, before any GenSeq core1 code.
> - At fault: only ~128 B of core1 stack used (SP `0x20040f80`), R0–R11 ≈ 0,
>   `r4=0` in the wrapper, exception frame unreadable (stacking itself faulted →
>   SP/entry was bad). SIO FIFO status `0xd0000050 = 0x0b` (ROE/read-on-empty set).
> - core0 DID complete the launch handshake (it's now wedged later, in
>   `restoreSequencerFromState`→`syncPitchSet`→FIFO push, because dead core1
>   never drains) — so core1 got a valid entry and started, then died instantly.
> - SCRATCH_X holds ONLY `core1_stack` (nothing overwrites the launch stack).
> - **ELIMINATED theories:** vector realloc in `addPattern`; core1 stack too small
>   (`PICO_CORE1_STACK_SIZE` bump had zero effect — core1 uses ~128 B before dying);
>   core0 overflow corrupting core1.
> - **More (2026-11-02, further probing):** HW breakpoints on `0x1001200c`
>   (core1_trampoline), `0x10012010` (core1_wrapper) and `0x10003594`
>   (sequencer_task), set before core0 launches core1, **NEVER FIRE** — core1
>   HardFaults *before* it reaches `core1_trampoline`, i.e. in the bootrom/SIO
>   FIFO launch handshake handoff itself (core1 pops a bad `pc` and jumps to
>   garbage). `r0` at fault varied across runs (0x0, 0x20040fa4). Core0 DOES
>   finish the handshake (it proceeds to restoreSequencerFromState), so the
>   handoff *completes* but delivers a corrupt entry to core1. The SIO FIFO
>   carried ROE (read-on-empty). The launch handshake is NOT reliably
>   single-steppable over this multidrop SWD link — mid-handshake register reads
>   return nonsense; do not trust them.
> - **RECOMMENDED next technique (don't keep SWD-stepping the handshake):**
>   instrument the FIRMWARE instead. (a) Put a GPIO/LED marker as the very first
>   thing `sequencer_task` does (toggle a trace pin before anything else) and a
>   second marker inside a minimal HardFault handler — a PicoScope/LA then shows
>   whether core1 ever reaches its entry and whether it enters the fault handler,
>   with NO SWD interference. (b) Temporarily replace `multicore_launch_core1`
>   with `multicore_launch_core1_with_stack` using an EXPLICIT stack, or add a
>   `multicore_fifo_drain()` + clear of the SIO FIFO error state (ROE/WOF) on
>   core0 immediately before launch, and test — if ROE is the culprit the clean
>   launch fixes it. (c) Bisect the M2→M3 SEQUENCER engine commits (0fb250d,
>   382c0e7, 1d042cd, f3b4513, 5543f4d, ed18609) by building each at its tip and
>   self-reset-testing — the launch path is byte-identical M2↔M3, so the trigger
>   is something the enlarged core1 Sequencer/engine does, not the launch code.
>   Self-reset loop (flash → wdreset.sh → probe) is agent-driven; use it.
> - **Leading unified read:** core1's bootrom pops a corrupt `pc` from its launch
>   stack handoff. What could corrupt that single word, given SCRATCH_X holds only
>   core1_stack and core0's stack is now healthy? Suspect the FIFO ROE/handshake
>   timing, or an M3 object whose static init (run on core0 before launch) writes
>   near `0x20040ff4`. Verify by reading `0x20040ff4..0x20041000` right AFTER the
>   push (breakpoint core0 just after `multicore_launch_core1_raw`'s push loop)
>   and confirming word[0]==0x10012010 (core1_wrapper), word[2]==entry.
>
> ### Bench method correction (IMPORTANT)
> Target is powered THROUGH the picoprobe, so a manual "power-cycle" = **replug the
> picoprobe USB**, and SWD `dap0` will NOT connect right after a replug until the
> rail settles. Use `adapter speed 2000` + a `sleep 2` before `init` (baked into
> the flash helper). `reset run` is still forbidden on this multidrop target.
>
> **HANDS-OFF RESET — CONFIRMED WORKING (no manual power-cycle needed).** Reboot
> the chip cold-equivalent over SWD via the WATCHDOG registers (this is NOT
> `reset run`; it re-runs boot2 + firmware like a real power-cycle). Sequence,
> after `init; halt`:
> ```
> mww 0x40010008 0x0001ffff   # PSM_WDSEL: watchdog resets the full power-on state machine
> mww 0x4005801c 0x00000000   # WATCHDOG_SCRATCH4: normal boot vector (no magic re-entry)
> mww 0x40058004 0x00000001   # WATCHDOG_LOAD: 1-tick timeout
> mww 0x40058000 0x80000000   # WATCHDOG_CTRL: TRIGGER immediate reset
> exit
> ```
> A trailing OpenOCD `Failed to write memory at 0xffffffff` is EXPECTED/harmless
> (the target resets out from under OpenOCD after the trigger). Verified 2026-11-02:
> M2 came up to banner + `H:pong` with no human at the bench. A bare
> `WATCHDOG_CTRL=0x80000000` TRIGGER ALONE is NOT enough — it reboots into a
> half-initialised dark/silent state; the PSM_WDSEL full-reset mask is what makes
> it clean. This supersedes every "ask the user to power-cycle" instruction
> elsewhere in this doc: the flow is flash → self-reset → probe, fully agent-driven.

## Status going in


- M3 is code-complete, **host-proven 41/41** (build under `$KIROCREW_SCRATCH`,
  NOT the stale `./build`). Latest commits: `f49ec9b` (v1 gate seeding),
  `c045c04` (core0 UIState by-reference fix), `0412245` / this doc (open bug).
- A **separate** core0 bug was already fixed (`c045c04`): UIState grew to ~1.4 KB
  and was copied by value in the reducer/handleEvent path → core0 2 KB stack
  overflow → host-control RX went dead. Now reduced in place. Do not re-chase it.

## The bug (deterministic, reproducible)

- M2 firmware (`ff900c9`) boots clean, LED matrix on.
- Latest M3 build flashed over the **same** FRAM record → boot fails, LED dark.
- SWD: core0 wedges at `multicore_fifo_wready` (blocked pushing a boot-sync
  command); **core1 launches then faults** (PC `0xfffffffe`, a fault/lockup
  handler — NOT bootrom `0x12a`). So core1 crashes **applying** a command from
  `restoreSequencerFromState`'s boot sync burst.
- The FRAM record is read **correctly**: latest's v1 decode order matches M2's v1
  serialize byte-for-byte, and the host `property7` v1-migration test passes. The
  loaded `PersistableConfig` is valid; the fault is downstream on core1.

## What changed on core1 in M3 (bisect these, most-suspect first)

1. **`addPattern` grows TWO std::vectors of enlarged objects** per `PATTERN_ADD`
   in the boot sync burst: `patterns` (now 160-byte `Pattern` with
   `GenerationParameters`) and the new `std::vector<PatternRng> patternRng`.
   `restoreSequencerFromState` sends one `PATTERN_ADD` per pattern, so a
   multi-pattern M2 record drives repeated reallocations on core1. **Prime suspect.**
2. `GEN_PARAMS_SET` decode in `command.cpp` `receiveCommandInto` / the
   `processCommand` apply.
3. `expandGateStates()` (replaced `createEuclidean` in the gate send path) and its
   interaction with the sync burst word counts.
4. `emitStep` / `reseedGenerationState` — NOT hit at boot (playing=false), lower
   priority, but `reseedGenerationState()` runs in `init()`.

## Bisection plan

1. Self-reset (board is on M2 now) → confirm LED on + `H:pong`. Build+flash latest
   M3, self-reset (watchdog sequence above), confirm LED dark + core1 fault
   (reproduce). Loop flash → self-reset → probe entirely agent-driven; no manual
   power-cycle needed.
2. Revert suspect #1 first: make `Sequencer` use a **fixed-capacity array** for
   `patternRng` (or `reserve(PATTERN_CAPACITY)` in the ctor) so `addPattern` does
   no reallocation, and confirm core1 boots.
3. If cleared → fix properly (reserve capacity up front, and/or
   `multicore_launch_core1_with_stack` with a larger explicit stack — the core0
   overflow proved the 2 KB default is marginal for the enlarged structures).
4. If not → re-add, bisect #2 then #3.
5. Commit each finding.

## Bench procedure (learned the hard way)

- Probe: Raspberry Pi Debugprobe (CMSIS-DAP) `2e8a:000c`; host-control UART bridge
  on `/dev/ttyACM0` @115200 (firmware stdio = UART0 GP0/GP1; `H:` channel in
  `src/ui/hardware/HostControl.cpp`; build `-DGENSEQ_HOST_CONTROL=ON`).
- **Flash:** `openocd -f interface/cmsis-dap.cfg -c 'adapter speed 4000' -f
  target/rp2040.cfg -c 'init; reset halt; program <elf> verify; exit'`. If program
  fails with a ROM/flash-erase error, the target is wedged — ensure `reset halt`
  ran first, retry.
- **Do NOT** boot via OpenOCD `reset run` — this multidrop RP2040 frequently faults
  on SWD-driven reset. **Reboot hands-off via the WATCHDOG registers instead**
  (see "HANDS-OFF RESET — CONFIRMED WORKING" in SESSION FINDINGS above):
  `init; halt; mww 0x40010008 0x0001ffff; mww 0x4005801c 0; mww 0x40058004 1;
  mww 0x40058000 0x80000000; exit`. This re-runs boot2 + firmware like a real
  power-cycle, needs no human, and releases SWD cleanly. A manual power-cycle
  (replug the picoprobe USB) is only a fallback. After reset, **stay off SWD**
  while testing serial (SWD init/halt disturbs the running firmware).
- Test host-control with no OpenOCD running: pyserial, open `/dev/ttyACM0` @115200,
  send `b'H:ping\n'`, expect `H:pong`. `H:key <name>` synthesizes keys through the
  real reducer; `H:view 3` = PitchSet.
- Keep the board on a working image when pausing (rebuild M2 from `ff900c9`:
  `cmake -B <dir> -DGENSEQ_HOST_CONTROL=ON; cmake --build <dir> --target genseq`).

## After core1 boots: finish task 10.4 silicon check

Over host-control: PitchSet view → set out-of-scale pitches → enable a scale with
QUANTIZE=SKIP → `H:play` → PicoScope (scopething; see `test/bench/capture_12_4_live.py`;
trace pins Tick=GP2 / MIDI=GP3 / Save=GP5; build with the trace `-D` flags from
`test/bench/README.md`). Verdict: note-on MIDI activity drops when the SKIP-scale
engages → `GEN_PARAMS_SET` reached core1 end-to-end. Then tasks 11.2/11.3 proceed.
