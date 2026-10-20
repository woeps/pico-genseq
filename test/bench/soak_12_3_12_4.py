#!/usr/bin/env python3
"""Tasks 12.3/12.4 soak gates — autonomous, via the host-control channel.

Runs the remaining Milestone-2 hardware gates hands-off and reports pass/fail:

  GATE A  BPM sweep transparency (12.3): at 40/120/255 BPM, confirm a live save
          does not disturb the tick cadence (inter-tick during save ~= baseline).
  GATE B  Edit storm (12.3): spam synthesized edit keys + saves; the channel must
          stay alive and the device's timingFault must never latch.
  GATE C  Endurance / power-cycle soak (12.4): N x (save -> watchdog reset);
          the persisted config must survive every cycle and timingFault stay 0.
  GATE D  Long-run playback (12.4): play for a duration; timingFault must stay 0.

Requires: firmware with trace pins + GENSEQ_HOST_CONTROL + a tick-pulse widen
(GENSEQ_TRACE_TICK_WIDEN_US>0 so the scope latches ticks), pyserial, scopething.
Usage: python3 soak_12_3_12_4.py [/dev/ttyACM0] [--cycles N] [--longrun SECS]
"""
import os, sys, time, threading, asyncio
sys.path.insert(0, os.environ.get("KIROCREW_SCRATCH", ".") + "/pylibs")
sys.path.insert(0, os.environ.get("KIROCREW_SCRATCH", ".") + "/scopething")
import serial, scope  # noqa: E402

PORT = sys.argv[1] if len(sys.argv) > 1 and not sys.argv[1].startswith("-") else "/dev/ttyACM0"
CYCLES = int(sys.argv[sys.argv.index("--cycles")+1]) if "--cycles" in sys.argv else 20
LONGRUN = int(sys.argv[sys.argv.index("--longrun")+1]) if "--longrun" in sys.argv else 60
BAUD = 115200

ser = serial.Serial(PORT, BAUD, timeout=0.1); time.sleep(0.4)
def hc(c, w=0.6):
    ser.reset_input_buffer(); ser.write(("H:"+c+"\n").encode()); ser.flush(); time.sleep(w)
    return ser.read(ser.in_waiting or 1).decode(errors="replace")
def dump():
    d = {}
    for ln in hc("dump").splitlines():
        if ln.startswith("D:") and "=" in ln:
            k, v = ln[2:].split("=", 1); d[k] = v
    return d
def ensure_playing():
    if dump().get("playing") != "1": hc("play"); time.sleep(0.3)

results = {}

# ---- GATE A: BPM sweep transparency ----------------------------------------
async def gate_a():
    """Transparency per tempo via a free-run inter-tick sampler (window 0.12s,
    under the BS05U trigger-timeout ceiling, works at 40/120/255 BPM). Saves fire
    continuously in the background; a transparent save keeps max inter-tick at
    the nominal period. Compares a quiet baseline against a saves-firing run."""
    import threading
    s = scope.Scope(); await s.connect()
    hc("testload", 1.2)
    ok_all = True; detail = {}
    for bpm in (40, 120, 255):
        hc(f"bpm {bpm}", 2.5); ensure_playing()
        nominal = 60000.0/(bpm*24)
        async def sample(n_caps):
            ivs=[]
            for _ in range(n_caps):
                try:
                    tr=await s.capture(channels=["L0"], period=0.12, timeout=0.15)
                except Exception:
                    continue
                T=[1 if x else 0 for x in tr["L0"].samples]; nn=len(T); dt=0.12/nn*1e6
                r=[i for i in range(1,nn) if T[i] and not T[i-1]]
                ivs += [(r[k+1]-r[k])*dt/1000 for k in range(len(r)-1)]
                await asyncio.sleep(0.05)
            return ivs
        base = await sample(8)
        stop=False
        def flood():
            while not stop:
                ser.write(b"H:save\n"); ser.flush(); time.sleep(0.2)
        th=threading.Thread(target=flood,daemon=True); th.start()
        during = await sample(10)
        stop=True; time.sleep(0.3)
        base_ok = base and max(base) < nominal*1.3
        save_ok = during and max(during) < nominal*1.3
        detail[bpm] = {"nominal_ms": round(nominal,2),
                       "baseline_max_ms": round(max(base),2) if base else None,
                       "during_save_max_ms": round(max(during),2) if during else None,
                       "n": len(during)}
        ok_all = ok_all and base_ok and save_ok
    results["A_bpm_sweep"] = ("PASS" if ok_all else "FAIL", detail)

# ---- GATE B: edit storm -----------------------------------------------------
def gate_b():
    hc("bpm 128"); ensure_playing()
    keys=["up","down","left","right","up","down"]
    for i in range(200):
        hc(f"key {keys[i%len(keys)]}", 0.0)  # fire-and-forget storm
        if i%25==0: ser.write(b"H:save\n"); ser.flush()
    time.sleep(0.5)
    alive = "H:pong" in hc("ping")
    d = dump()
    ok = alive and d.get("timingFault")=="0"
    results["B_edit_storm"] = ("PASS" if ok else "FAIL",
                               {"channel_alive": alive, "timingFault": d.get("timingFault")})

# ---- GATE C: endurance / power-cycle soak -----------------------------------
def gate_c(cycles):
    hc("bpm 199", 2.5); ensure_playing()
    before = dump()
    survived = 0; faults = 0
    for c in range(cycles):
        ser.write(b"H:save\n"); ser.flush(); time.sleep(0.3)
        ser.write(b"H:reset\n"); ser.flush(); ser.close(); time.sleep(3.5)
        globals()["ser"] = serial.Serial(PORT, BAUD, timeout=0.1); time.sleep(0.3)
        hc("ping")
        d = dump()
        if d.get("bpm")==before.get("bpm") and d.get("activePatterns")==before.get("activePatterns"):
            survived += 1
        if d.get("timingFault")=="1": faults += 1
    ok = survived==cycles and faults==0
    results["C_endurance"] = ("PASS" if ok else "FAIL",
                              {"cycles": cycles, "config_survived": survived, "faults": faults})

# ---- GATE D: long-run playback ---------------------------------------------
def gate_d(secs):
    hc("bpm 255", 2.5); hc("testload", 1.2); ensure_playing()
    faults = 0; end = time.time()+secs
    while time.time() < end:
        time.sleep(5)
        if dump().get("timingFault")=="1": faults += 1
    results["D_longrun"] = ("PASS" if faults==0 else "FAIL",
                            {"secs": secs, "faults_seen": faults})

def main():
    print(f"SOAK 12.3/12.4 on {PORT} (cycles={CYCLES} longrun={LONGRUN}s)")
    if "H:pong" not in hc("ping"): print("FAIL: channel dead"); return 1
    asyncio.run(gate_a())
    gate_b()
    gate_c(CYCLES)
    gate_d(LONGRUN)
    print("\n===== SOAK RESULTS =====")
    allpass = True
    for k,(verdict,detail) in results.items():
        print(f"{verdict}  {k}: {detail}")
        allpass = allpass and verdict=="PASS"
    print("\nMILESTONE-2 SOAK:", "ALL GATES PASS" if allpass else "SOME GATES FAILED")
    return 0 if allpass else 1

if __name__ == "__main__":
    sys.exit(main())
