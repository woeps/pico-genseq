#!/usr/bin/env python3
"""Autonomous 12.3 boot-load round-trip (FRAM backend).

Drives the firmware's host-control serial channel (commit f699ec8 protocol:
"H:" command prefix, "D:key=value" dump, "D:end" terminator) over the picoprobe
UART bridge — no keyboard, no human power-cycle:

    H:bpm N -> H:dump (before) -> H:save (FRAM A/B commit) -> H:reset (watchdog
    self-reboot, re-runs the boot load) -> reconnect -> H:dump (after)
    -> assert bpm survived.

Requires a firmware built with -DGENSEQ_HOST_CONTROL=ON. Pyserial only.
Usage: python3 roundtrip_12_3.py [/dev/ttyACM0] [target_bpm]
"""
import sys
import time
import serial  # pyserial

PORT = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyACM0"
TARGET_BPM = int(sys.argv[2]) if len(sys.argv) > 2 else 177
BAUD = 115200


def drain(ser, deadline_s=1.0):
    out, buf = [], b""
    end = time.time() + deadline_s
    while time.time() < end:
        n = ser.in_waiting
        if n:
            buf += ser.read(n)
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                s = line.decode(errors="replace").strip("\r")
                if s:
                    out.append(s)
            end = time.time() + 0.3
        else:
            time.sleep(0.02)
    return out


def send(ser, cmd, wait=0.8):
    # Commands require the "H:" guard prefix (committed HostControl protocol).
    ser.reset_input_buffer()
    ser.write(("H:" + cmd + "\n").encode())
    ser.flush()
    return drain(ser, wait)


def parse_dump(lines):
    d = {}
    for ln in lines:
        if ln.startswith("D:") and "=" in ln:
            k, v = ln[2:].split("=", 1)
            d[k] = v
    return d


def main():
    print(f"[12.3] port={PORT} target_bpm={TARGET_BPM}")
    ser = serial.Serial(PORT, BAUD, timeout=0.1)
    time.sleep(0.3)

    if not any(l == "H:pong" for l in send(ser, "ping")):
        print("FAIL: no H:pong — not a host-control build, or wrong port")
        return 1
    print("  H:ping -> H:pong OK")

    send(ser, f"bpm {TARGET_BPM}", wait=2.5)
    before = parse_dump(send(ser, "dump"))
    print(f"  before save: {before}")
    if before.get("bpm") != str(TARGET_BPM):
        print(f"FAIL: could not set bpm to {TARGET_BPM} (got {before.get('bpm')})")
        return 1

    send(ser, "save", wait=1.5)
    print("  H:save -> FRAM A/B commit issued")

    send(ser, "reset", wait=0.5)
    print("  H:reset -> watchdog reboot; waiting for re-enumeration...")
    ser.close()
    time.sleep(4.0)

    for _ in range(10):
        try:
            ser = serial.Serial(PORT, BAUD, timeout=0.1)
            break
        except serial.SerialException:
            time.sleep(0.5)
    else:
        print("FAIL: could not reopen port after reset")
        return 1
    time.sleep(0.3)
    send(ser, "ping")  # flush boot noise
    after = parse_dump(send(ser, "dump"))
    print(f"  after reboot: {after}")

    # The persisted fields that must survive: bpm, patternCount, activePatterns.
    checks = {
        "bpm": str(TARGET_BPM),
        "patternCount": before.get("patternCount"),
        "activePatterns": before.get("activePatterns"),
    }
    fails = [k for k, v in checks.items() if after.get(k) != v]
    print()
    if not fails:
        print(f"PASS: bpm={TARGET_BPM}, patternCount={checks['patternCount']}, "
              f"activePatterns={checks['activePatterns']} all survived save + "
              f"reboot (boot-load restored from FRAM). 12.3 round-trip VERIFIED.")
        return 0
    print(f"FAIL: fields did not survive reboot: "
          f"{ {k: (before.get(k), after.get(k)) for k in fails} }")
    return 1


if __name__ == "__main__":
    sys.exit(main())
