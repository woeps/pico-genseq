#!/usr/bin/env python3
"""Task 11.3 (part a) -- per-feature live control on silicon.

For EVERY Generation Parameter (design.md section 9, R10 control map), drive its
real matrix-UI key path over the host-control channel WHILE PLAYBACK IS LIVE under
the dense `testload` pattern, then:

  1. SET it from its default and confirm the COMMITTED value changed (H:dump gen*),
  2. CLEAR it back through the same UI control and confirm the default returns
     (R10.3 clear-to-default, R10.6 demonstrated set-and-clear path),
  3. after every step: timingFault stays 0 and the UI loop stays within its bound,
  4. at the end: Stop leaves 0 sounding notes (no stuck note, R6.3).

Every edit streams to core1 through the M2 command/snapshot path (sendGateSet /
sendPitchSet / sendVelocitySet / sendGenParams), i.e. the boundary-apply path R10.5
requires. Needs a GENSEQ_HOST_CONTROL=ON firmware whose H:dump exposes gen* fields.

Usage: python3 livecontrol_11_3.py [/dev/ttyACM0] [--bpm 200]
"""
import sys, time
import serial

PORT = sys.argv[1] if len(sys.argv) > 1 and not sys.argv[1].startswith("-") else "/dev/ttyACM0"
BPM = int(sys.argv[sys.argv.index("--bpm") + 1]) if "--bpm" in sys.argv else 200
UI_GAP_LIMIT_US = 50_000

ser = serial.Serial(PORT, 115200, timeout=0.1)
time.sleep(0.4)


def hc(cmd, wait=0.25):
    ser.reset_input_buffer()
    ser.write(("H:" + cmd + "\n").encode())
    ser.flush()
    time.sleep(wait)
    return ser.read(ser.in_waiting or 1).decode(errors="replace")


def dump():
    d = {}
    for ln in hc("dump", 0.5).splitlines():
        if ln.startswith("D:") and "=" in ln:
            k, v = ln[2:].split("=", 1)
            d[k] = v.strip()
    return d


def keys(*seq):
    """seq items: 'left', 'up', 'up:coarse', 'enter', 'escape', 'f3' ..."""
    for k in seq:
        name, _, mod = k.partition(":")
        if name in ("up", "down", "left", "right"):
            hc(f"{name} {'coarse' if mod == 'coarse' else ''}".strip(), 0.12)
        else:
            hc(f"key {name}", 0.12)


def to_patterns_on(pattern_set):
    """Go to the PATTERNS view with GATE / PITCH / VELOCITY selected."""
    keys("f3")
    keys("left", "up")              # PITCH->GATE, VELOCITY->GATE  => GATE
    if pattern_set == "PITCH":
        keys("right")
    elif pattern_set == "VELOCITY":
        keys("down")


def rep(k, n):
    return [k] * n


# (name, field, open-set, SET key path (after ENTER opened the editor), expected
#  set value, CLEAR key path, default value). Editors: GateSet opens on ALGORITHM
# (PROBABILITY = 6 right, STEP_GRID = 7 right; ENTER on the grid TOGGLES, so leave
# the grid with LEFT before committing). PitchSet/VelocitySet open on COUNT (=3).
STEP_INHERIT = "ff"
CONTROLS = [
    ("Scale id", "genScale", "PITCH",
     rep("left", 3) + ["up", "enter"], "1",
     rep("left", 3) + ["down", "enter"], "0"),
    ("Scale root", "genRoot", "PITCH",
     rep("left", 2) + ["up", "up", "enter"], "2",
     rep("left", 2) + ["down", "down", "enter"], "0"),
    ("Quantize mode", "genQuantize", "PITCH",
     rep("left", 1) + rep("up", 3) + ["enter"], "3",            # NEAREST -> SKIP
     rep("left", 1) + rep("down", 3) + ["enter"], "0"),
    ("Per-Pattern probability", "genProb", "GATE",
     rep("right", 6) + rep("down:coarse", 3) + ["enter"], "70",
     rep("right", 6) + rep("up:coarse", 4) + ["enter"], "100"),
    ("Per-step probability (step 0)", "genStepProb", "GATE",
     rep("right", 7) + rep("up", 6) + ["left", "enter"], "32",   # INHERIT -> 50% (0x32)
     rep("right", 7) + rep("down", 6) + ["left", "enter"], STEP_INHERIT),
    ("Per-step gate (step 0)", "genGates", "GATE",
     rep("right", 7) + ["enter", "left", "enter"], None,        # toggle (value-relative)
     rep("right", 7) + ["enter", "left", "enter"], None),
    ("Humanize range", "genHumanize", "VELOCITY",
     rep("left", 3) + rep("up:coarse", 2) + ["enter"], "20",
     rep("left", 3) + rep("down:coarse", 2) + ["enter"], "0"),
    ("Seed", "genSeed", "VELOCITY",                              # no-seed -> seed 5
     rep("left", 2) + rep("up", 6) + ["enter"], "5",
     rep("left", 2) + rep("down", 6) + ["enter"], "0"),
    ("Coupling", "genCoupling", "VELOCITY",
     rep("left", 1) + ["up", "enter"], "1",                      # INDEPENDENT -> LOCKED
     rep("left", 1) + ["down", "enter"], "0"),
]


def field_value(d, field):
    v = d.get(field)
    if field == "genStepProb" and v:
        return v[0:2]           # step 0 byte
    if field == "genGates" and v:
        return v[0]             # step 0 gate bit
    return v


def apply(path, open_set):
    to_patterns_on(open_set)
    keys("enter")               # open the editor for the selected pattern set
    keys(*path)
    keys("escape")              # clean (just committed) -> back to PATTERNS


def health(tag, log):
    d = dump()
    gap = int(d.get("maxUiGapUs", "0") or 0)
    ok = d.get("timingFault") == "0" and gap <= UI_GAP_LIMIT_US and d.get("playing") == "1"
    log.append((tag, ok, f"timingFault={d.get('timingFault')} maxUiGapUs={gap} playing={d.get('playing')}"))
    return ok, d


def main():
    print(f"[11.3a] port={PORT} bpm={BPM}")
    if "H:pong" not in hc("ping", 0.5):
        print("FAIL: no H:pong")
        return 2
    hc("testload", 1.4)
    hc(f"bpm {BPM}", 2.5)
    if dump().get("playing") != "1":
        hc("play", 0.6)
    hc("resetmetrics", 0.3)
    if dump().get("selectedPattern") != "0":
        print("note: selectedPattern != 0; results are for the selected pattern")

    rows, health_log, all_ok = [], [], True
    for name, field, open_set, set_path, set_val, clr_path, def_val in CONTROLS:
        before = field_value(dump(), field)
        apply(set_path, open_set)
        h1, d1 = health(f"{name}: after set", health_log)
        after_set = field_value(d1, field)
        apply(clr_path, open_set)
        h2, d2 = health(f"{name}: after clear", health_log)
        after_clr = field_value(d2, field)

        if set_val is None:      # toggle control: set must differ, clear must restore
            set_ok = after_set is not None and after_set != before
            clr_ok = after_clr == before
        else:
            set_ok = after_set == set_val and before == def_val
            clr_ok = after_clr == def_val
        if field == "genSeed":   # seed also needs seedSet 1 -> 0
            set_ok = set_ok and d1.get("genSeedSet") == "1"
            clr_ok = clr_ok and d2.get("genSeedSet") == "0"
        ok = set_ok and clr_ok and h1 and h2
        all_ok = all_ok and ok
        rows.append((name, before, after_set, after_clr, ok))
        print(f"  {'PASS' if ok else 'FAIL'}  {name:32s} default={before!s:4} set={after_set!s:4} cleared={after_clr!s:4}")

    final = dump()
    hc("play", 0.8)              # toggle -> Stop
    time.sleep(0.6)
    stopped = dump()
    stuck_ok = stopped.get("playing") == "0" and stopped.get("soundingNotes") == "0"
    gap = int(final.get("maxUiGapUs", "0") or 0)
    print(f"  {'PASS' if stuck_ok else 'FAIL'}  Stop -> soundingNotes={stopped.get('soundingNotes')} (no stuck note)")
    print(f"  max UI-loop gap over the whole run: {gap} us (limit {UI_GAP_LIMIT_US})")
    for tag, ok, info in health_log:
        if not ok:
            print(f"  HEALTH FAIL {tag}: {info}")
    all_ok = all_ok and stuck_ok and gap <= UI_GAP_LIMIT_US
    print("\n11.3a LIVE CONTROL:", "ALL PASS" if all_ok else "SOME FAILED")
    return 0 if all_ok else 1


sys.exit(main())
