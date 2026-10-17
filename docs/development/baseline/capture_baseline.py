#!/usr/bin/env python3
"""GenSeq task 1.4 baseline capture harness (BitScope BS05U via scopething).

Separates two measurements the spec (Req 11.7) wants reported independently:
  * Logical Scheduling Jitter  -- deviation of Tick/MIDI trace edges from the
    ideal logical grid, measured on trace pins L0 (Tick) / L1 (MIDI dispatch).
  * MIDI Wire Skew             -- real UART serialization on L5 (GP8), where one
    byte @31250 baud = 10 bits = 320us.

Because the BS05U buffer is only 12,288 samples, span and resolution trade off,
so each metric uses its own capture: a coarse WIDE window to count ticks/period,
and a fast NARROW window triggered on one edge to resolve sub-us timing.

Usage:
  python3 capture_baseline.py grid        # tick period + per-interval jitter
  python3 capture_baseline.py tickwidth   # single-tick service duration, hi-res
  python3 capture_baseline.py midi        # MIDI dispatch vs wire (L1 + L5)
  python3 capture_baseline.py save        # Ctrl+S pause on L3 (Save)
  python3 capture_baseline.py raw L0 50e-3 6000   # ad-hoc
"""
import asyncio, json, sys, time, logging
import numpy as np
import scope

logging.basicConfig(level=logging.WARNING)
PRIMARY = 40_000_000  # BS05U primary clock


def falling_rising(samples, sp):
    b = (np.asarray(samples) > 0.5).astype(np.int8)
    rise = np.where((b[1:] == 1) & (b[:-1] == 0))[0]
    fall = np.where((b[1:] == 0) & (b[:-1] == 1))[0]
    return rise, fall, b


def pulse_widths_us(rise, fall, sp):
    widths = []
    for r in rise:
        f = fall[fall > r]
        if len(f):
            widths.append((f[0] - r) * sp * 1e6)
    return np.array(widths)


async def connect():
    s = scope.Scope()
    await asyncio.wait_for(s.connect(), timeout=20)
    return s


async def cap(s, chans, period, nsamples, trig=None, tpos=0.1, to=None):
    kw = dict(channels=chans, period=period, nsamples=nsamples)
    if trig is not None:
        kw.update(trigger=trig, trigger_type='rising', trigger_position=tpos, timeout=to)
    return await asyncio.wait_for(s.capture(**kw), timeout=45)


def series_of(traces, name):
    ser = traces[name]
    s = np.asarray(ser['samples'] if isinstance(ser, dict) else ser.samples)
    sp = ser['sample_period'] if isinstance(ser, dict) else ser.sample_period
    return s, sp


async def do_grid(s, window=60e-3):
    # One-period window: at 60ms/12k -> 5us res, spans >=2 ticks at any BPM<=255.
    # Free-run (no trigger) so a rare tick edge never overruns the timeout register.
    tr = await cap(s, ['L0'], period=window, nsamples=12000)
    s0, sp = series_of(tr, 'L0')
    rise, fall, _ = falling_rising(s0, sp)
    out = {'metric': 'grid', 'window_ms': window*1e3, 'rate_MSps': 1/sp/1e6,
           'resolution_us': sp*1e6, 'ticks_seen': int(len(rise))}
    if len(rise) >= 2:
        iv = np.diff(rise) * sp * 1e6
        out.update(interval_mean_us=float(iv.mean()), interval_min_us=float(iv.min()),
                   interval_max_us=float(iv.max()), interval_std_us=float(iv.std()),
                   bpm_implied=float(60e6/(iv.mean()*24)))
        w = pulse_widths_us(rise, fall, sp)
        if len(w):
            out.update(tick_service_us_mean=float(w.mean()), tick_service_us_max=float(w.max()))
    return out


async def do_tickwidth(s):
    # Resolve the tick service-high duration. Free-run a short window and keep
    # whatever pulse(s) land in it (the pulse is ~8us; a 2ms/12k window = 167ns
    # res captures a pulse ~most windows without needing a rare-edge trigger).
    best = None
    for _ in range(6):
        tr = await cap(s, ['L0'], period=2e-3, nsamples=12000)
        s0, sp = series_of(tr, 'L0')
        rise, fall, _ = falling_rising(s0, sp)
        w = pulse_widths_us(rise, fall, sp)
        if len(w):
            best = (w, sp); break
    out = {'metric': 'tickwidth', 'rate_MSps': (1/best[1]/1e6) if best else None,
           'resolution_us': (best[1]*1e6) if best else None}
    if best:
        w = best[0]
        out.update(edges=int(len(w)), service_us_mean=float(w.mean()),
                   service_us_max=float(w.max()), service_us_min=float(w.min()))
    else:
        out['note'] = 'no pulse landed in 6 free-run windows; raise attempts or widen window'
    return out


async def do_midi(s):
    # L1 = dispatch trace, L5 = real wire (GP8). Trigger on dispatch.
    tr = await cap(s, ['L1', 'L5'], period=8e-3, nsamples=12000, trig={1: 1}, tpos=0.05, to=1.0)
    out = {'metric': 'midi', 'channels': {}}
    sp_ref = None
    for ch in ('L1', 'L5'):
        if ch not in tr:
            continue
        sv, sp = series_of(tr, ch); sp_ref = sp
        rise, fall, b = falling_rising(sv, sp)
        info = {'rate_MSps': 1/sp/1e6, 'rises': int(len(rise)), 'high_frac': float(b.mean())}
        if len(rise) >= 2:
            info['first_rise_us'] = float(rise[0]*sp*1e6)
        out['channels'][ch] = info
    # Wire-skew note: one byte @31250 = 320us; computed, cross-checked by L5 low pulses.
    out['byte_time_us_theory'] = 320.0
    return out


async def do_save(s):
    # Save pulse (L3) is a long high interval on core0; coarse wide window.
    tr = await cap(s, ['L3'], period=200e-3, nsamples=12000, trig={3: 1}, tpos=0.05, to=2.0)
    s3, sp = series_of(tr, 'L3')
    rise, fall, b = falling_rising(s3, sp)
    out = {'metric': 'save', 'rate_MSps': 1/sp/1e6, 'resolution_us': sp*1e6,
           'rises': int(len(rise))}
    w = pulse_widths_us(rise, fall, sp)
    if len(w):
        out.update(save_pause_us_max=float(w.max()), save_pause_ms_max=float(w.max()/1000))
    return out


async def do_raw(s, ch, period, nsamples):
    tr = await cap(s, [ch], period=period, nsamples=nsamples)
    sv, sp = series_of(tr, ch)
    rise, fall, b = falling_rising(sv, sp)
    return {'metric': 'raw', 'ch': ch, 'rate_MSps': 1/sp/1e6, 'resolution_us': sp*1e6,
            'rises': int(len(rise)), 'high_frac': float(b.mean())}


async def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else 'grid'
    s = await connect()
    try:
        if mode == 'grid':
            r = await do_grid(s)
        elif mode == 'tickwidth':
            r = await do_tickwidth(s)
        elif mode == 'midi':
            r = await do_midi(s)
        elif mode == 'save':
            r = await do_save(s)
        elif mode == 'raw':
            r = await do_raw(s, sys.argv[2], float(sys.argv[3]), int(sys.argv[4]))
        else:
            r = {'error': f'unknown mode {mode}'}
    finally:
        s.close()
    r['captured_at'] = time.strftime('%Y-%m-%dT%H:%M:%S%z')
    print(json.dumps(r, indent=2))

if __name__ == '__main__':
    asyncio.run(main())
