# Bench test harnesses

Host-side scripts that drive the firmware on real hardware over the serial
**host-control channel** (see `src/ui/hardware/HostControl.*`, built with
`-DGENSEQ_HOST_CONTROL=ON`). The channel synthesizes the same input events a
keyboard would and self-resets via the watchdog, so these run with no keypress
and no human power-cycle.

## `roundtrip_12_3.py` — task 12.3 boot-load round-trip (FRAM)

Proves the external-FRAM persistence path end-to-end: set a distinctive config,
commit it (the real Ctrl+S / A/B transactional commit), self-reboot, and confirm
the boot load restores it from FRAM.

```
H:bpm N -> H:dump -> H:save -> H:reset -> (reconnect) -> H:dump -> assert survived
```

Verifies `bpm`, `patternCount` and `activePatterns` all survive save + reboot
(`view` correctly resets — it is transient, not persisted).

### Run

```bash
# 1. Build + flash a host-control firmware (over the picoprobe/SWD):
cmake -B build-hostctl -DGENSEQ_HOST_CONTROL=ON
cmake --build build-hostctl --target genseq
openocd -f interface/cmsis-dap.cfg -c "adapter speed 5000" -f target/rp2040.cfg \
        -c "program build-hostctl/genseq.elf verify reset exit"
# 2. One clean power-cycle (the SWD program+reset multidrop quirk); thereafter
#    H:reset makes every run hands-off.
# 3. Run the round-trip (pyserial required):
python3 test/bench/roundtrip_12_3.py /dev/ttyACM0 177
```

`/dev/ttyACM0` is the picoprobe's UART bridge to the target's UART0 (GP0/GP1),
which carries genseq's stdio. Expect `PASS` and exit 0. Re-run with any BPM in
[40,255]; distinct successive values confirm the A/B slot alternation.

## `soak_12_3_12_4.py` — tasks 12.3/12.4 soak gates

Runs the remaining Milestone-2 hardware gates hands-off and reports pass/fail:

- **A — BPM sweep transparency** (12.3): at 40/120/255 BPM, the max inter-tick
  interval while saves fire equals the quiet baseline equals the nominal period
  (no stall at any tempo). Free-run tick sampling (window under the BS05U
  trigger-timeout ceiling) so slow tempos still measure.
- **B — edit storm** (12.3): 200 synthesized edit keys + saves; channel stays
  alive and `timingFault` never latches.
- **C — endurance / power-cycle soak** (12.4): N× `save` → watchdog `reset`; the
  persisted config must survive every cycle, zero faults.
- **D — long-run playback** (12.4): play for SECS; zero faults.

Needs a firmware built with trace pins + `GENSEQ_HOST_CONTROL` +
`GENSEQ_TRACE_TICK_WIDEN_US=200` (so the scope latches the ~5.5 µs tick pulse).

```bash
cmake -B build-12_4 -DGENSEQ_PERFORMANCE_TRACE_ENABLED=ON \
  -DGENSEQ_TRACE_TICK_PIN=2 -DGENSEQ_TRACE_MIDI_PIN=3 -DGENSEQ_TRACE_SAVE_PIN=5 \
  -DGENSEQ_TRACE_TICK_WIDEN_US=200 -DGENSEQ_HOST_CONTROL=ON
cmake --build build-12_4 --target genseq
# flash (hands-off reset via the chip watchdog — OpenOCD's own reset wedges this RP2040):
openocd -f interface/cmsis-dap.cfg -c "adapter speed 5000" -f target/rp2040.cfg \
        -c "init; halt; program build-12_4/genseq.elf verify; exit"
openocd -f interface/cmsis-dap.cfg -c "adapter speed 5000" -f target/rp2040.cfg \
        -c "init; halt; mww 0x40010008 0x0001ffff; mww 0x40058008 1; mww 0x40058000 0x80000000; exit"
python3 test/bench/soak_12_3_12_4.py /dev/ttyACM0 --cycles 20 --longrun 60
```
