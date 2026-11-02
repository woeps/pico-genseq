# Debug brief: core1 boot fault (M3) — bisection handoff

Focused hardware bisection for a reproducible RP2040 **core1 boot fault** in
pico-genseq Milestone 3. Everything needed to start cold is here.

Repo: `/home/chris/Projects/pico/pico-genseq`, branch `agents`.

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
> - **Next hypotheses to test (fresh session):** a global/`static` **constructor or
>   `runtime_run_per_core_initializers` step** that faults on core1 specifically
>   (e.g. something touching a peripheral/flag core1 shouldn't, or a `__not_in_flash`
>   / SCRATCH object init); OR core1 taking a fault on its very first real memory
>   access (bad VTOR-relative or a data object placed where core1 can't reach it
>   yet). Technique that worked: `openocd` @2 MHz with a **2 s settle** (target is
>   powered THROUGH the picoprobe, so the rail must stabilise before the multidrop
>   target-select — see Bench procedure), HW breakpoints on `0x1001200c`
>   (core1_trampoline) / `0x10012010` (core1_wrapper) / `0x10003594`
>   (sequencer_task) set BEFORE core0 launches, then catch core1's first
>   instruction and single-step into the faulting one. Cross-check against the
>   M2→M3 diff of per-core-init / global-ctor-bearing files.
>
> ### Bench method correction (IMPORTANT)
> Target is powered THROUGH the picoprobe, so "power-cycle" = **replug the
> picoprobe USB**, and SWD `dap0` will NOT connect right after a replug until the
> rail settles. Use `adapter speed 2000` + a `sleep 2` before `init` (baked into
> the flash helper). `reset run` is still forbidden on this multidrop target.

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

1. Power-cycle (board is on M2 now) → confirm LED on. Build+flash latest M3,
   power-cycle, confirm LED dark + core1 fault (reproduce).
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
  on SWD-driven reset. Ask the USER to physically power-cycle for a clean boot,
  then **stay off SWD** while testing serial (SWD init/halt disturbs the running
  firmware).
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
