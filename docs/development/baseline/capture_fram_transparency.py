#!/usr/bin/env python3
"""FRAM save transparency capture (task 9.3, Route A).

Triggers on the Save pin (GP5 -> logic channel L3) rising and captures a window
wide enough to contain the full ~1.81 ms FRAM write plus margin on both sides.
Then it checks the decisive question: do Tick (GP2 -> L0) and MIDI (GP3 -> L1)
keep toggling DURING the Save-high window? For internal flash (task 9.2) they
went dark for ~37 ms. FRAM should keep them alive.

Logic channel mapping (scopething logic index == BitScope Lx):
    L0 = GP2 = Tick
    L1 = GP3 = MIDI
    L3 = GP5 = Save (trigger)

Trigger form (proven in 9.2): a dict {channel_index: level}, so L3 rising = {3: 1}.
Config kept under the BS05U U16 trigger-timeout ceiling: trigger_position small,
timeout modest (the 9.2 lesson: a wide window + large timeout overflows the reg).
"""
import asyncio
import sys
import os

sys.path.insert(0, os.environ.get("KIROCREW_SCRATCH", ".") + "/pylibs")
sys.path.insert(0, ".")

import scope  # noqa: E402

# Logic channel indices
TICK = 0   # L0 / GP2
MIDI = 1   # L1 / GP3
SAVE = 3   # L3 / GP5  (trigger)

# The FRAM write measured ~1.81 ms. Capture a window that holds it plus margin.
WINDOW_S = 0.044          # 44 ms window holds the ~32 ms stress write + margin
TRIGGER_POS = 0.08        # ~3.5 ms pre-roll
TIMEOUT_S = 0.12


async def main():
    s = scope.Scope()
    await s.connect()
    print(f"connected: {getattr(s, 'url', '?')}")

    attempts = 200
    for n in range(attempts):
        try:
            traces = await s.capture(
                channels=[f"L{TICK}", f"L{MIDI}", f"L{SAVE}"],
                period=WINDOW_S,
                trigger={SAVE: 1},            # L3 rising
                trigger_position=TRIGGER_POS,
                timeout=TIMEOUT_S,
            )
        except scope.ConfigurationError as e:
            print(f"CONFIG ERROR (fatal, not a timeout): {e}")
            return
        except Exception as e:
            if "timeout" in str(e).lower():
                continue
            print(f"arm {n}: unexpected error: {e}")
            continue

        def series(name):
            t = traces.get(name)
            if t is None:
                return None
            return [1 if x else 0 for x in t.samples]

        tick = series(f"L{TICK}")
        midi = series(f"L{MIDI}")
        save = series(f"L{SAVE}")
        if not save or 1 not in save:
            continue

        total = len(save)
        dt_us = (WINDOW_S / total) * 1e6

        hi_start = save.index(1)
        hi_end = hi_start
        for i in range(hi_start, total):
            if save[i]:
                hi_end = i
        save_us = (hi_end - hi_start) * dt_us
        if save_us < 20000:
            # Short window = the 1.8 ms real save; wait for the 32 ms STRESS write.
            continue

        def edges_in(series_, a, b):
            c = 0
            prev = series_[a] if a < len(series_) else 0
            for i in range(a + 1, min(b + 1, len(series_))):
                if series_[i] != prev:
                    c += 1
                prev = series_[i]
            return c

        tick_edges_during = edges_in(tick, hi_start, hi_end) if tick else 0
        midi_edges_during = edges_in(midi, hi_start, hi_end) if midi else 0

        def longest_low_gap(series_):
            best = 0
            run = 0
            for v in series_:
                if v == 0:
                    run += 1
                    best = max(best, run)
                else:
                    run = 0
            return best * dt_us

        tick_max_gap_us = longest_low_gap(tick) if tick else 0.0

        print("=== CAPTURE (arm %d) ===" % n)
        print(f"samples={total} dt={dt_us:.2f}us window={WINDOW_S*1e3:.1f}ms")
        print(f"Save-high (FRAM write) = {save_us/1000:.3f} ms [{hi_start}..{hi_end}]")
        print(f"Tick edges DURING write = {tick_edges_during}")
        print(f"MIDI edges DURING write = {midi_edges_during}")
        print(f"longest Tick-low gap in window = {tick_max_gap_us/1000:.3f} ms")
        verdict = ("TRANSPARENT: Tick/MIDI kept toggling through the FRAM write"
                   if tick_edges_during > 0
                   else "STALL: Tick went dark during the write (like flash)")
        print("VERDICT:", verdict)

        out = os.environ.get("KIROCREW_SCRATCH", ".") + "/fram_transparency_capture.txt"
        with open(out, "w") as f:
            f.write(f"dt_us={dt_us}\nsave_hi={hi_start},{hi_end}\n")
            f.write("idx,tick,midi,save\n")
            for i in range(total):
                f.write(f"{i},{tick[i] if tick else 0},"
                        f"{midi[i] if midi else 0},{save[i]}\n")
        print("raw saved:", out)
        return

    print(f"no Save edge captured in {attempts} arms")


if __name__ == "__main__":
    asyncio.run(main())
