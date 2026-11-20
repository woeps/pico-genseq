#!/usr/bin/env python3
"""Task 11.3 (part b) -- seeded determinism on silicon (R4.2/R4.3/R4.5).

Pattern 0 is given RNG-driven material through its real matrix-UI controls:
per-Pattern Step Probability 50 % (trigger/suppress rolls) + Velocity
Humanization 20 (velocity jitter), with Deterministic Seed 7. The decoded MIDI
note stream is captured from the firmware's host-control MIDI tap (the exact
bytes handed to the MIDI UART) and compared:

  A  fresh Play from Stop
  A2 Stop -> Play again in the same boot          (R4.5 restart re-seeds)
  B  H:save -> watchdog reboot -> FRAM boot load   (R4.3 saved seed reproduces)
  S  sensitivity: seed 8 must DIFFER from seed 7   (proves the capture sees the RNG)

Modes:
  python3 determinism_11_3.py [/dev/ttyACM0]                 full run, saves ref to --ref
  python3 determinism_11_3.py [/dev/ttyACM0] --after-power-cycle
        only capture C after a MANUAL cold power-cycle and compare to the saved ref
Options: --ref PATH (default ./determinism_11_3_ref.txt), --secs N (default 8)
"""
import sys, time
import serial

ARGS = sys.argv[1:]
PORT = ARGS[0] if ARGS and not ARGS[0].startswith("-") else "/dev/ttyACM0"
REF = ARGS[ARGS.index("--ref") + 1] if "--ref" in ARGS else "determinism_11_3_ref.txt"
SECS = float(ARGS[ARGS.index("--secs") + 1]) if "--secs" in ARGS else 8.0
AFTER_PC = "--after-power-cycle" in ARGS
BPM = 200
MIN_EVENTS = 40

ser = None


def connect(retries=20):
    global ser
    for _ in range(retries):
        try:
            ser = serial.Serial(PORT, 115200, timeout=0.1)
            time.sleep(0.4)
            if "H:pong" in hc("ping", 0.5):
                return True
            ser.close()
        except Exception:
            pass
        time.sleep(0.5)
    return False


def hc(cmd, wait=0.25):
    ser.reset_input_buffer()
    ser.write(("H:" + cmd + "\n").encode())
    ser.flush()
    time.sleep(wait)
    out = b""
    t0 = time.time()
    while time.time() - t0 < wait + 0.6:
        chunk = ser.read(4096)
        if chunk:
            out += chunk
            if cmd.startswith("midilog dump") and b"M:end" in out:
                break
        elif out and not cmd.startswith("midilog dump"):
            break
    return out.decode(errors="replace")


def dump():
    d = {}
    for ln in hc("dump", 0.5).splitlines():
        if ln.startswith("D:") and "=" in ln:
            k, v = ln[2:].split("=", 1)
            d[k] = v.strip()
    return d


def keys(*seq):
    for k in seq:
        name, _, mod = k.partition(":")
        if name in ("up", "down", "left", "right"):
            hc(f"{name} {'coarse' if mod == 'coarse' else ''}".strip(), 0.12)
        else:
            hc(f"key {name}", 0.12)


def isolate_pattern0():
    """Delete every pattern but 0 through the real PatternsView DELETE key, so
    unseeded patterns cannot pollute the shared-channel capture."""
    for _ in range(8):
        d = dump()
        n = int(d.get("patternCount", "1"))
        if n <= 1:
            return d
        hc("key f3", 0.12)
        for _ in range(n):
            hc("key right ctrl", 0.12)  # clamps at the last pattern / add slot
        hc("key left ctrl", 0.12)       # back onto the last real pattern
        hc("key delete", 0.2)
    return dump()


def to_patterns_on(ps):
    keys("f3")
    for _ in range(4):
        hc("key left ctrl", 0.12)       # select pattern 0
    keys("left", "up")
    if ps == "PITCH":
        keys("right")
    elif ps == "VELOCITY":
        keys("down")


def edit(ps, path):
    to_patterns_on(ps)
    keys("enter", *path, "escape")


def set_playing(want):
    if (dump().get("playing") == "1") != want:
        hc("play", 0.6)


def configure(seed):
    """Drive the R10 controls from whatever state to: prob 50, humanize 20, seed."""
    set_playing(False)
    d = dump()
    prob = int(d.get("genProb", "100"))
    steps = (prob - 50) // 10
    if steps > 0:
        edit("GATE", ["right"] * 6 + ["down:coarse"] * steps + ["enter"])
    elif steps < 0:
        edit("GATE", ["right"] * 6 + ["up:coarse"] * (-steps) + ["enter"])
    hum = int(d.get("genHumanize", "0"))
    if hum != 20:
        delta = 20 - hum
        mv = ["up"] * delta if delta > 0 else ["down"] * (-delta)
        edit("VELOCITY", ["left"] * 3 + mv + ["enter"])
    # Seed: walk down to "no seed" first, then up to the target (no-seed -> 0 -> seed).
    cur = int(d.get("genSeed", "0")) if d.get("genSeedSet") == "1" else -1
    down = ["down"] * (cur + 1)
    up = ["up"] * (seed + 1)
    edit("VELOCITY", ["left"] * 2 + down + up + ["enter"])
    d = dump()
    ok = d.get("genProb") == "50" and d.get("genHumanize") == "20" and \
        d.get("genSeedSet") == "1" and d.get("genSeed") == str(seed)
    return ok, d


def capture():
    set_playing(False)
    time.sleep(0.4)
    hc("midilog start", 0.2)
    hc("play", 0.2)
    time.sleep(SECS)
    hc("play", 0.4)  # Stop (releases owned notes, emitted into the log too)
    time.sleep(0.3)
    raw = hc("midilog dump", 1.5)
    data = bytearray()
    for ln in raw.splitlines():
        if ln.startswith("M:") and not ln.startswith(("M:n=", "M:end")):
            data += bytes.fromhex(ln[2:].strip())
    return decode(bytes(data))


def decode(b):
    """-> list of (status, data1, data2) channel messages; realtime bytes dropped."""
    ev, i, run = [], 0, None
    while i < len(b):
        x = b[i]
        if x >= 0xF8:
            i += 1
            continue
        if x >= 0x80:
            run = x
            i += 1
        if run is None or i + 1 >= len(b) + 1:
            break
        hi = run & 0xF0
        n = 2 if hi in (0x80, 0x90, 0xA0, 0xB0, 0xE0) else 1
        if i + n > len(b):
            break
        ev.append((run,) + tuple(b[i:i + n]))
        i += n
    return ev


def summary(ev):
    on = [e for e in ev if (e[0] & 0xF0) == 0x90 and e[2] > 0]
    vel = sorted({e[2] for e in on})
    return f"{len(ev)} events, {len(on)} note-ons, velocities {vel[:8]}{'...' if len(vel) > 8 else ''}"


def same(a, b):
    n = min(len(a), len(b))
    if n >= MIN_EVENTS and a[:n] != b[:n]:
        i = next(k for k in range(n) if a[k] != b[k])
        print(f"    first difference at event {i}: "
              f"{[' '.join(f'{x:02x}' for x in e) for e in a[max(0, i - 2):i + 3]]} vs "
              f"{[' '.join(f'{x:02x}' for x in e) for e in b[max(0, i - 2):i + 3]]}")
    return n >= MIN_EVENTS and a[:n] == b[:n], n


def main():
    print(f"[11.3b] port={PORT} secs={SECS} mode={'after-power-cycle' if AFTER_PC else 'full'}")
    if not connect():
        print("FAIL: no H:pong")
        return 2
    if AFTER_PC:
        d = dump()
        print(f"  loaded after power-cycle: prob={d.get('genProb')} humanize={d.get('genHumanize')} "
              f"seedSet={d.get('genSeedSet')} seed={d.get('genSeed')} bpm={d.get('bpm')}")
        ref = [tuple(int(x, 16) for x in ln.split()) for ln in open(REF) if ln.strip()]
        c = capture()
        ok, n = same(ref, c)
        print(f"  C (cold power-cycle + load): {summary(c)}")
        print(f"  {'PASS' if ok else 'FAIL'}  C == A over first {n} events")
        print("\n11.3b DETERMINISM (power-cycle):", "PASS" if ok else "FAIL")
        return 0 if ok else 1

    hc(f"bpm {BPM}", 2.5)
    set_playing(False)
    d = isolate_pattern0()
    print(f"  isolated pattern 0: patternCount={d.get('patternCount')} activePatterns={d.get('activePatterns')}")
    if d.get("patternCount") != "1" or d.get("activePatterns") != "0x0001":
        print("FAIL: could not isolate pattern 0")
        return 1
    ok7, d = configure(7)
    print(f"  configured seed 7 via UI: {ok7}  (prob={d.get('genProb')} humanize={d.get('genHumanize')} "
          f"seedSet={d.get('genSeedSet')} seed={d.get('genSeed')})")
    if not ok7:
        print("FAIL: could not configure via the R10 controls")
        return 1
    a = capture()
    print(f"  A  fresh play           : {summary(a)}")
    a2 = capture()
    print(f"  A2 stop->play restart   : {summary(a2)}")

    hc("save", 1.2)
    ser.write(b"H:reset\n"); ser.flush(); ser.close(); time.sleep(3.5)
    if not connect():
        print("FAIL: no H:pong after reboot")
        return 2
    d = dump()
    print(f"  after save+reboot load : prob={d.get('genProb')} humanize={d.get('genHumanize')} "
          f"seedSet={d.get('genSeedSet')} seed={d.get('genSeed')}")
    b = capture()
    print(f"  B  reboot + FRAM load   : {summary(b)}")

    ok8, _ = configure(8)
    s8 = capture()
    print(f"  S  seed 8 (sensitivity) : {summary(s8)}")
    configure(7)
    hc("save", 1.2)  # leave seed 7 persisted for the --after-power-cycle step

    with open(REF, "w") as f:
        for e in a:
            f.write(" ".join(f"{x:02x}" for x in e) + "\n")

    r1, n1 = same(a, a2)
    r2, n2 = same(a, b)
    diff = not same(a, s8)[0]
    print(f"  {'PASS' if r1 else 'FAIL'}  A == A2 over first {n1} events (restart re-seeds, R4.5)")
    print(f"  {'PASS' if r2 else 'FAIL'}  A == B  over first {n2} events (saved seed reproduces, R4.3)")
    print(f"  {'PASS' if diff else 'FAIL'}  seed 8 != seed 7 (capture is RNG-sensitive)")
    ok = r1 and r2 and diff and ok8
    print(f"  reference saved to {REF} (run --after-power-cycle after a cold power-cycle)")
    print("\n11.3b DETERMINISM:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


sys.exit(main())
