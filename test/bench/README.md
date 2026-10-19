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
