#!/usr/bin/env python3
"""9.2 coincident-burst save capture.

Hardware-trigger on L3 (Save). Capture L0 (tick), L3 (save), L5 (MIDI wire)
with a window wide enough to see the wire BEFORE, DURING and AFTER the save,
so we can answer the note-safety question: does the ~37 ms freeze STRETCH an
in-flight note (note-on just before the freeze, note-off pushed ~37 ms late
after it releases) -> stuck-note risk?

Press Ctrl+S on the GenSeq while this loop runs; the triggered arm watches L3
near-continuously until it catches the edge.
"""
import asyncio
import sys
import numpy as np
import scope

# BS05U logic channels (match the trace wiring):
#   L0 = GP2  tick (core1)
#   L3 = GP5  save (core0)   <- trigger
#   L5 = GP8  MIDI TX wire
CH = ['L0', 'L3', 'L5']
TRIGGER = 'L3'

PERIOD = 0.120          # seconds captured per window
SAMPLES = 11999         # -> ~10 us resolution
PRE = 0.05              # fraction before trigger -> ~6 ms pre-roll
MAX_ARMS = 220


def edges(sig, t):
    b = sig > 0.5
    d = np.diff(b.astype(np.int8))
    return t[1:][d == 1], t[1:][d == -1]


async def one_capture(s):
    # Logic-channel trigger is a dict {channel_index: level}; L3 -> index 3, rising to high.
    # Keep trigger_position + timeout small so trigger_timeout stays under the U16 ceiling.
    return await s.capture(
        channels=CH, period=PERIOD, nsamples=SAMPLES,
        trigger={3: 1}, trigger_type='rising',
        trigger_position=PRE, timeout=0.15,
    )


async def main():
    s = scope.Scope()
    await s.connect()
    caught = None
    for arm in range(MAX_ARMS):
        try:
            traces = await one_capture(s)
        except scope.ConfigurationError as e:
            print(f"CONFIG ERROR (aborting): {e}", file=sys.stderr)
            return 2
        except TimeoutError:
            continue
        except Exception as e:
            msg = str(e).lower()
            # Only a genuine 'no edge within window' timeout is skippable.
            if 'timed out' in msg or msg == 'timeout':
                continue
            print(f"arm {arm}: {type(e).__name__}: {e}", file=sys.stderr)
            continue
        t = np.array(traces[CH[0]].timestamps)
        l3 = np.array(traces['L3'].samples)
        r3, _ = edges(l3, t)
        if len(r3) == 0:
            continue
        caught = (t, traces)
        print(f"CAUGHT save edge on arm {arm}")
        break
    if caught is None:
        print("NO SAVE EDGE CAUGHT")
        return 1

    t, traces = caught
    l0 = np.array(traces['L0'].samples)
    l3 = np.array(traces['L3'].samples)
    l5 = np.array(traces['L5'].samples)

    r3, f3 = edges(l3, t)
    save_start = r3[0]
    save_end = f3[f3 > save_start][0] if np.any(f3 > save_start) else t[-1]
    save_ms = (save_end - save_start) * 1e3
    truncated = not np.any(f3 > save_start)

    rw, fw = edges(l5, t)
    wire_edges = np.sort(np.concatenate([rw, fw]))
    before = wire_edges[wire_edges < save_start]
    during = wire_edges[(wire_edges >= save_start) & (wire_edges <= save_end)]
    after = wire_edges[wire_edges > save_end]

    last_before = before[-1] if len(before) else None
    first_after = after[0] if len(after) else None

    r0, _ = edges(l0, t)
    ticks_during = int(np.sum((r0 >= save_start) & (r0 <= save_end)))

    stretch_gap_ms = None
    if last_before is not None and first_after is not None:
        stretch_gap_ms = (first_after - last_before) * 1e3

    print("=== 9.2 coincident-burst save ===")
    print(f"save_pause_ms           = {save_ms:.2f}")
    print(f"save_pulse_truncated    = {truncated}")
    print(f"ticks_during_save       = {ticks_during}")
    print(f"wire_edges_before       = {len(before)}")
    print(f"wire_edges_during       = {len(during)}")
    print(f"wire_edges_after        = {len(after)}")
    print(f"last_wire_before_save_ms= {None if last_before is None else round((last_before-save_start)*1e3,3)}")
    print(f"first_wire_after_save_ms= {None if first_after is None else round((first_after-save_end)*1e3,3)}")
    print(f"wire_silence_gap_ms     = {None if stretch_gap_ms is None else round(stretch_gap_ms,2)}")

    out = "/home/chris/Projects/pico/pico-genseq/docs/development/baseline/captures/save_coincident_255bpm_10pat.npy"
    np.save(out, np.vstack([t, l0, l3, l5]))
    print(f"saved {out}")
    return 0


sys.exit(asyncio.run(main()))
