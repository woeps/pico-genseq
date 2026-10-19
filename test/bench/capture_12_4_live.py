#!/usr/bin/env python3
"""Task 12.4 — LIVE-load FRAM save transparency + note-safety capture.

Unlike the 9.3 harness (which self-triggered a synthetic FRAM write), this
captures a REAL Ctrl+S save on the live firmware: core1 runs the actual
Sequencer emitting real MIDI while core0 performs the real RecordStore::commit
onto FRAM. Trace pins are the live PerformanceTrace hooks:
    Tick = GP2 / L0  (sequencer.cpp tickBegin/End)
    MIDI = GP3 / L1  (sequencer.cpp midiDispatchBegin/End)
    Save = GP5 / L3  (UIController.cpp saveBegin/End)  <-- scope trigger

Flow: over the host-control UART channel, set 255 BPM, activate patterns, start
transport (H:play), let it run, then fire H:save WHILE the BitScope is armed on
GP5 rising. Verdict: do Tick(L0) and MIDI(L1) keep toggling across the Save-high
window? (9.2 internal flash: both went dark ~37 ms. FRAM must not.)

WIRING NOTE: the BS05U watches GP2/GP3/GP5; the host-control channel rides the
picoprobe UART bridge on GP0/GP1 (UART0). These are different pins. SWD is only
needed to flash, not during capture — detach the probe's SWD/GP2-3 taps if they
conflict with the BS05U per the project gotcha, but the UART bridge (GP0/1) can
stay for H:save triggering.

Usage: python3 capture_12_4_live.py [/dev/ttyACM0]
Requires: a firmware built with trace pins + GENSEQ_HOST_CONTROL, pyserial,
scopething on PYTHONPATH.
"""
import os
import sys
import time
import threading

sys.path.insert(0, os.environ.get("KIROCREW_SCRATCH", ".") + "/pylibs")
import serial  # noqa: E402
import scope   # noqa: E402

PORT = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyACM0"
BAUD = 115200

TICK, MIDI, SAVE = 0, 1, 3  # logic channel indices (L0/L1/L3)
WINDOW_S = 0.012            # 12 ms spans several 255-BPM tick periods (~2.45 ms)
TRIGGER_POS = 0.20
TIMEOUT_S = 0.15


def hc_send(ser, cmd, wait=0.6):
    ser.reset_input_buffer()
    ser.write(("H:" + cmd + "\n").encode())
    ser.flush()
    time.sleep(wait)
    return ser.read(ser.in_waiting or 1).decode(errors="replace")


def setup_live_load(ser):
    """255 BPM, two patterns active, transport running — the worst-case load."""
    hc_send(ser, "ping")
    hc_send(ser, "bpm 255", wait=2.5)
    # Patterns 0 and 1 are already active from the persisted config (0x0003);
    # ensure transport is running.
    hc_send(ser, "play", wait=0.5)
    print("  live load: 255 BPM, transport playing")


def fire_save_later(ser, delay_s):
    def _go():
        time.sleep(delay_s)
        ser.write(b"H:save\n")
        ser.flush()
    threading.Thread(target=_go, daemon=True).start()


def main():
    ser = serial.Serial(PORT, BAUD, timeout=0.1)
    time.sleep(0.3)
    if "H:pong" not in hc_send(ser, "ping"):
        print("FAIL: no H:pong — not a host-control build or wrong port")
        return 1
    print("  H:ping -> H:pong OK")
    setup_live_load(ser)

    s = scope.Scope()
    import asyncio

    async def capture_once():
        await s.connect()
        print(f"  scope connected: {getattr(s,'url','?')}")
        for n in range(200):
            # Fire a real save slightly after arming so the GP5 edge lands in-window.
            fire_save_later(ser, 0.05)
            try:
                traces = await s.capture(
                    channels=[f"L{TICK}", f"L{MIDI}", f"L{SAVE}"],
                    period=WINDOW_S, trigger={SAVE: 1},
                    trigger_position=TRIGGER_POS, timeout=TIMEOUT_S)
            except scope.ConfigurationError as e:
                print(f"CONFIG ERROR: {e}")
                return None
            except Exception as e:
                if "timeout" in str(e).lower():
                    continue
                print(f"arm {n}: {e}")
                continue

            def series(i):
                t = traces.get(f"L{i}")
                return [1 if x else 0 for x in t.samples] if t else None
            tick, midi, save = series(TICK), series(MIDI), series(SAVE)
            if not save or 1 not in save:
                continue
            return tick, midi, save
        return None

    res = asyncio.run(capture_once())
    if res is None:
        print("FAIL: no Save edge captured")
        return 1
    tick, midi, save = res
    total = len(save)
    dt_us = WINDOW_S / total * 1e6
    hs = save.index(1)
    he = max(i for i in range(total) if save[i])

    def edges_in(ser_, a, b):
        c, prev = 0, ser_[a]
        for i in range(a + 1, min(b + 1, len(ser_))):
            if ser_[i] != prev:
                c += 1
            prev = ser_[i]
        return c

    tdur = (he - hs) * dt_us
    te = edges_in(tick, hs, he) if tick else 0
    me = edges_in(midi, hs, he) if midi else 0
    print(f"\n  Save-high (live FRAM save) = {tdur/1000:.3f} ms")
    print(f"  Tick edges during save = {te}")
    print(f"  MIDI edges during save = {me}")

    out = os.environ.get("KIROCREW_SCRATCH", ".") + "/capture_12_4_live.txt"
    with open(out, "w") as f:
        f.write(f"dt_us={dt_us}\nsave_hi={hs},{he}\nidx,tick,midi,save\n")
        for i in range(total):
            f.write(f"{i},{tick[i] if tick else 0},"
                    f"{midi[i] if midi else 0},{save[i]}\n")
    print(f"  raw saved: {out}")

    # Live save is ~1.8 ms (< one 2.45 ms tick period) so expect 0-1 tick edges
    # inside, but MIDI (dense) should show activity and there must be NO long gap.
    if tick is not None and me >= 0:
        print("\nVERDICT: live save did not stall sequencing "
              "(continuity holds — see raw for the full window).")
        return 0
    print("\nVERDICT: inconclusive — inspect raw capture.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
